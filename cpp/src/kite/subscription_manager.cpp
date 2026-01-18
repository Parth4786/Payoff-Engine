/**
 * @file subscription_manager.cpp
 * @brief Kite WebSocket Subscription Manager Implementation
 * 
 * Manages token subscriptions across multiple Kite WebSocket connections.
 * 
 * Key Features:
 * - Maps exchange_token (ClickHouse) ↔ instrument_token (Kite WS)
 * - Connection pooling: 3 WebSocket connections per client
 * - Token limit: 3000 tokens max per WebSocket connection
 * - Multi-credential support
 * 
 * Reference: docs/Market-observatory/data_sources/kite_ws_source.py
 */

#include "kite/subscription_manager.hpp"
#include "core/config.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>

namespace payoff::kite {

// ============================================================================
// Constructor / Destructor
// ============================================================================

KiteSubscriptionManager::KiteSubscriptionManager(
    std::shared_ptr<core::InstrumentManager> instrument_manager,
    SubscriptionManagerConfig config)
    : instrument_manager_(std::move(instrument_manager)),
      config_(std::move(config)) {
    
    if (!instrument_manager_) {
        throw std::invalid_argument("InstrumentManager cannot be null");
    }
}

KiteSubscriptionManager::~KiteSubscriptionManager() {
    stop();
}

// ============================================================================
// Credential Management
// ============================================================================

int KiteSubscriptionManager::add_credentials(const KiteCredentials& creds) {
    if (!creds.is_valid()) {
        std::cerr << "[SubscriptionManager] Invalid credentials provided\n";
        return -1;
    }
    
    std::unique_lock lock(pools_mutex_);
    
    // Check for duplicate API key
    for (size_t i = 0; i < client_pools_.size(); ++i) {
        if (client_pools_[i]->credentials.api_key == creds.api_key) {
            std::cerr << "[SubscriptionManager] Duplicate API key: " << creds.api_key << "\n";
            return -1;
        }
    }
    
    auto pool = std::make_unique<ClientPool>();
    pool->credentials = creds;
    
    // Create WebSocket slots (but don't connect yet)
    for (size_t i = 0; i < config_.max_ws_per_client; ++i) {
        pool->slots[i].ws = std::make_unique<KiteWebSocket>(
            creds.api_key, creds.access_token);
        pool->slots[i].ws->set_auto_reconnect(
            config_.auto_reconnect,
            config_.max_reconnect_retries,
            config_.reconnect_delay_ms);
    }
    
    int index = static_cast<int>(client_pools_.size());
    client_pools_.push_back(std::move(pool));
    
    // If already running, set up callbacks for the new pool
    if (running_) {
        auto& new_pool = client_pools_.back();
        for (size_t slot_idx = 0; slot_idx < config_.max_ws_per_client; ++slot_idx) {
            setup_websocket_callbacks(*new_pool, slot_idx);
        }
        new_pool->is_active = true;
    }
    
    std::cout << "[SubscriptionManager] Added credentials for API key: " 
              << creds.api_key.substr(0, 8) << "... (index: " << index << ")\n";
    
    return index;
}

int KiteSubscriptionManager::add_credentials_from_config() {
    auto& cfg = config::config();
    if (!cfg.is_loaded()) {
        cfg.load();
    }
    
    KiteCredentials creds;
    creds.api_key = cfg.kite_api_key();
    creds.access_token = cfg.kite_access_token();
    
    if (!creds.is_valid()) {
        std::cerr << "[SubscriptionManager] No Kite credentials in config\n";
        return -1;
    }
    
    return add_credentials(creds);
}

void KiteSubscriptionManager::remove_credentials(size_t index) {
    std::unique_lock lock(pools_mutex_);
    
    if (index >= client_pools_.size()) {
        return;
    }
    
    auto& pool = client_pools_[index];
    
    // Disconnect all slots
    for (auto& slot : pool->slots) {
        if (slot.ws) {
            slot.ws->disconnect();
        }
        slot.subscribed_tokens.clear();
    }
    
    // Remove subscriptions assigned to this pool
    {
        std::unique_lock sub_lock(subscriptions_mutex_);
        for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
            if (it->second.client_index == index) {
                instrument_to_canonical_.erase(it->second.instrument_token);
                it = subscriptions_.erase(it);
            } else {
                // Adjust indices for pools after the removed one
                if (it->second.client_index > index) {
                    it->second.client_index--;
                }
                ++it;
            }
        }
    }
    
    pool->is_active = false;
    client_pools_.erase(client_pools_.begin() + static_cast<ptrdiff_t>(index));
    
    // Redistribute remaining tokens if running
    if (running_ && config_.auto_redistribute) {
        lock.unlock();
        redistribute_tokens();
    }
}

size_t KiteSubscriptionManager::credential_count() const noexcept {
    std::shared_lock lock(pools_mutex_);
    return client_pools_.size();
}

size_t KiteSubscriptionManager::total_capacity() const noexcept {
    std::shared_lock lock(pools_mutex_);
    return client_pools_.size() * config_.max_ws_per_client * config_.max_tokens_per_ws;
}

size_t KiteSubscriptionManager::subscription_count() const noexcept {
    std::shared_lock lock(subscriptions_mutex_);
    return subscriptions_.size();
}

// ============================================================================
// Subscription Management
// ============================================================================

bool KiteSubscriptionManager::subscribe(const std::string& canonical_symbol) {
    // Resolve instrument_token
    auto instrument_token = resolve_instrument_token(canonical_symbol);
    if (!instrument_token) {
        std::cerr << "[SubscriptionManager] Cannot resolve instrument_token for: " 
                  << canonical_symbol << "\n";
        return false;
    }
    
    std::unique_lock lock(subscriptions_mutex_);
    
    // Check if already subscribed
    auto it = subscriptions_.find(canonical_symbol);
    if (it != subscriptions_.end() && it->second.is_subscribed) {
        return true;  // Already subscribed
    }
    
    // Parse exchange_token from canonical symbol
    uint32_t exchange_token = 0;
    auto pos = canonical_symbol.find(':');
    if (pos != std::string::npos) {
        try {
            exchange_token = static_cast<uint32_t>(std::stoul(canonical_symbol.substr(pos + 1)));
        } catch (...) {}
    }
    
    // Create or update entry
    SubscriptionEntry entry;
    entry.canonical_symbol = canonical_symbol;
    entry.exchange_token = exchange_token;
    entry.instrument_token = *instrument_token;
    entry.is_subscribed = false;
    
    // Find available slot
    {
        std::shared_lock pools_lock(pools_mutex_);
        auto assignment = find_available_slot();
        if (!assignment.found) {
            std::cerr << "[SubscriptionManager] No available slot for subscription\n";
            return false;
        }
        entry.client_index = assignment.client_index;
        entry.ws_index = assignment.slot_index;
    }
    
    subscriptions_[canonical_symbol] = entry;
    instrument_to_canonical_[*instrument_token] = canonical_symbol;
    
    // Apply subscription to WebSocket
    bool success = apply_subscription(subscriptions_[canonical_symbol]);
    
    if (success) {
        // Notify callback
        std::lock_guard cb_lock(callback_mutex_);
        if (status_callback_) {
            status_callback_(canonical_symbol, true);
        }
    }
    
    return success;
}

size_t KiteSubscriptionManager::subscribe_batch(const std::vector<std::string>& canonical_symbols) {
    size_t count = 0;
    
    // Batch by target slot for efficiency
    std::map<std::pair<size_t, size_t>, std::vector<std::string>> by_slot;
    
    for (const auto& symbol : canonical_symbols) {
        auto instrument_token = resolve_instrument_token(symbol);
        if (!instrument_token) continue;
        
        // Find slot assignment (simplified: just subscribe one by one for now)
        if (subscribe(symbol)) {
            ++count;
        }
    }
    
    return count;
}

size_t KiteSubscriptionManager::subscribe_option_chain(
    const std::string& underlying,
    std::optional<std::chrono::year_month_day> expiry) {
    
    if (!instrument_manager_) return 0;
    
    auto chain = instrument_manager_->get_option_chain(underlying, expiry);
    
    std::vector<std::string> symbols;
    symbols.reserve(chain.size());
    
    for (const auto* inst : chain) {
        symbols.push_back(inst->canonical_symbol());
    }
    
    std::cout << "[SubscriptionManager] Subscribing to " << symbols.size() 
              << " instruments for " << underlying << " option chain\n";
    
    return subscribe_batch(symbols);
}

void KiteSubscriptionManager::unsubscribe(const std::string& canonical_symbol) {
    std::unique_lock lock(subscriptions_mutex_);
    
    auto it = subscriptions_.find(canonical_symbol);
    if (it == subscriptions_.end()) {
        return;
    }
    
    apply_unsubscription(it->second);
    instrument_to_canonical_.erase(it->second.instrument_token);
    subscriptions_.erase(it);
    
    // Notify callback
    {
        std::lock_guard cb_lock(callback_mutex_);
        if (status_callback_) {
            status_callback_(canonical_symbol, false);
        }
    }
}

void KiteSubscriptionManager::unsubscribe_batch(const std::vector<std::string>& canonical_symbols) {
    for (const auto& symbol : canonical_symbols) {
        unsubscribe(symbol);
    }
}

void KiteSubscriptionManager::unsubscribe_option_chain(
    const std::string& underlying,
    std::optional<std::chrono::year_month_day> expiry) {
    
    if (!instrument_manager_) return;
    
    auto chain = instrument_manager_->get_option_chain(underlying, expiry);
    
    std::vector<std::string> symbols;
    for (const auto* inst : chain) {
        symbols.push_back(inst->canonical_symbol());
    }
    
    unsubscribe_batch(symbols);
}

void KiteSubscriptionManager::replace_subscriptions(const std::vector<std::string>& new_symbols) {
    std::set<std::string> new_set(new_symbols.begin(), new_symbols.end());
    std::vector<std::string> to_unsubscribe;
    std::vector<std::string> to_subscribe;
    
    {
        std::shared_lock lock(subscriptions_mutex_);
        
        // Find symbols to unsubscribe
        for (const auto& [symbol, entry] : subscriptions_) {
            if (new_set.find(symbol) == new_set.end()) {
                to_unsubscribe.push_back(symbol);
            }
        }
        
        // Find symbols to subscribe
        for (const auto& symbol : new_symbols) {
            if (subscriptions_.find(symbol) == subscriptions_.end()) {
                to_subscribe.push_back(symbol);
            }
        }
    }
    
    std::cout << "[SubscriptionManager] Replacing subscriptions: "
              << "unsubscribe " << to_unsubscribe.size() 
              << ", subscribe " << to_subscribe.size() << "\n";
    
    unsubscribe_batch(to_unsubscribe);
    subscribe_batch(to_subscribe);
}

void KiteSubscriptionManager::unsubscribe_all() {
    std::vector<std::string> all_symbols;
    
    {
        std::shared_lock lock(subscriptions_mutex_);
        all_symbols.reserve(subscriptions_.size());
        for (const auto& [symbol, _] : subscriptions_) {
            all_symbols.push_back(symbol);
        }
    }
    
    unsubscribe_batch(all_symbols);
}

// ============================================================================
// Query
// ============================================================================

bool KiteSubscriptionManager::is_subscribed(const std::string& canonical_symbol) const {
    std::shared_lock lock(subscriptions_mutex_);
    auto it = subscriptions_.find(canonical_symbol);
    return it != subscriptions_.end() && it->second.is_subscribed;
}

std::vector<std::string> KiteSubscriptionManager::get_subscribed_symbols() const {
    std::shared_lock lock(subscriptions_mutex_);
    std::vector<std::string> result;
    result.reserve(subscriptions_.size());
    for (const auto& [symbol, entry] : subscriptions_) {
        if (entry.is_subscribed) {
            result.push_back(symbol);
        }
    }
    return result;
}

std::optional<SubscriptionEntry> KiteSubscriptionManager::get_subscription(
    const std::string& canonical_symbol) const {
    
    std::shared_lock lock(subscriptions_mutex_);
    auto it = subscriptions_.find(canonical_symbol);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

KiteSubscriptionManager::Stats KiteSubscriptionManager::get_stats() const {
    Stats stats;
    
    {
        std::shared_lock lock(pools_mutex_);
        stats.total_credentials = client_pools_.size();
        
        for (const auto& pool : client_pools_) {
            for (const auto& slot : pool->slots) {
                if (slot.ws && slot.ws->is_connected()) {
                    ++stats.active_connections;
                }
                stats.total_subscribed += slot.token_count();
            }
        }
        stats.total_capacity = client_pools_.size() * 
                               config_.max_ws_per_client * 
                               config_.max_tokens_per_ws;
    }
    
    stats.ticks_received = total_ticks_.load();
    stats.last_tick = last_tick_time_;
    
    return stats;
}

// ============================================================================
// Connection Management
// ============================================================================

void KiteSubscriptionManager::start() {
    if (running_.exchange(true)) {
        return;  // Already running
    }
    
    std::shared_lock lock(pools_mutex_);
    
    for (size_t client_idx = 0; client_idx < client_pools_.size(); ++client_idx) {
        auto& pool = client_pools_[client_idx];
        
        for (size_t slot_idx = 0; slot_idx < config_.max_ws_per_client; ++slot_idx) {
            auto& slot = pool->slots[slot_idx];
            
            if (!slot.ws) continue;
            
            setup_websocket_callbacks(*pool, slot_idx);
            
            // Only connect slots that have subscriptions
            if (!slot.subscribed_tokens.empty()) {
                std::cout << "[SubscriptionManager] Connecting client " << client_idx 
                          << " slot " << slot_idx << " with " 
                          << slot.subscribed_tokens.size() << " tokens\n";
                slot.ws->connect();
            }
        }
        
        pool->is_active = true;
    }
    
    std::cout << "[SubscriptionManager] Started\n";
}

void KiteSubscriptionManager::stop() {
    if (!running_.exchange(false)) {
        return;  // Not running
    }
    
    std::shared_lock lock(pools_mutex_);
    
    for (auto& pool : client_pools_) {
        for (auto& slot : pool->slots) {
            if (slot.ws) {
                slot.ws->disconnect();
            }
        }
        pool->is_active = false;
    }
    
    std::cout << "[SubscriptionManager] Stopped\n";
}

bool KiteSubscriptionManager::is_running() const noexcept {
    return running_.load();
}

void KiteSubscriptionManager::redistribute_tokens() {
    std::unique_lock pools_lock(pools_mutex_);
    std::unique_lock subs_lock(subscriptions_mutex_);
    
    std::cout << "[SubscriptionManager] Redistributing " << subscriptions_.size() << " tokens\n";
    
    // Collect all subscriptions
    std::vector<std::string> all_symbols;
    for (const auto& [symbol, _] : subscriptions_) {
        all_symbols.push_back(symbol);
    }
    
    // Clear current assignments
    for (auto& pool : client_pools_) {
        for (auto& slot : pool->slots) {
            if (slot.ws) {
                // Unsubscribe all
                std::vector<uint32_t> tokens(slot.subscribed_tokens.begin(), 
                                              slot.subscribed_tokens.end());
                if (!tokens.empty()) {
                    slot.ws->unsubscribe(tokens);
                }
            }
            slot.subscribed_tokens.clear();
        }
    }
    
    // Reset subscription states
    for (auto& [_, entry] : subscriptions_) {
        entry.is_subscribed = false;
    }
    
    subs_lock.unlock();
    pools_lock.unlock();
    
    // Re-subscribe all
    subscribe_batch(all_symbols);
}

// ============================================================================
// Callbacks
// ============================================================================

void KiteSubscriptionManager::on_snapshot(SubscriptionSnapshotCallback callback) {
    std::lock_guard lock(callback_mutex_);
    snapshot_callback_ = std::move(callback);
}

void KiteSubscriptionManager::on_error(SubscriptionErrorCallback callback) {
    std::lock_guard lock(callback_mutex_);
    error_callback_ = std::move(callback);
}

void KiteSubscriptionManager::on_status(SubscriptionStatusCallback callback) {
    std::lock_guard lock(callback_mutex_);
    status_callback_ = std::move(callback);
}

// ============================================================================
// Internal Methods
// ============================================================================

void KiteSubscriptionManager::setup_websocket_callbacks(ClientPool& pool, size_t slot_index) {
    auto& slot = pool.slots[slot_index];
    if (!slot.ws) return;
    
    // Find client index
    size_t client_idx = 0;
    {
        std::shared_lock lock(pools_mutex_);
        for (size_t i = 0; i < client_pools_.size(); ++i) {
            if (client_pools_[i].get() == &pool) {
                client_idx = i;
                break;
            }
        }
    }
    
    slot.ws->on_ticks([this](const KiteTick& tick) {
        handle_tick(tick);
    });
    
    slot.ws->on_connect([this, client_idx, slot_index]() {
        handle_connect(client_idx, slot_index);
    });
    
    slot.ws->on_disconnect([this, client_idx, slot_index](int code, const std::string& reason) {
        handle_disconnect(client_idx, slot_index, code, reason);
    });
    
    slot.ws->on_error([this, client_idx, slot_index](const std::string& error) {
        handle_error(client_idx, slot_index, error);
    });
}

void KiteSubscriptionManager::handle_tick(const KiteTick& tick) {
    ++total_ticks_;
    last_tick_time_ = std::chrono::steady_clock::now();
    
    // Look up canonical symbol
    std::string canonical_symbol;
    {
        std::shared_lock lock(subscriptions_mutex_);
        auto it = instrument_to_canonical_.find(tick.instrument_token);
        if (it != instrument_to_canonical_.end()) {
            canonical_symbol = it->second;
            
            // Update subscription entry
            auto sub_it = subscriptions_.find(canonical_symbol);
            if (sub_it != subscriptions_.end()) {
                sub_it->second.last_tick = last_tick_time_;
                sub_it->second.tick_count++;
            }
        }
    }
    
    // Convert tick to snapshot and forward
    core::DepthSnapshot snapshot = tick.to_snapshot();
    
    if (!canonical_symbol.empty()) {
        snapshot.symbol = canonical_symbol;
        
        // Set instrument_id to exchange_token for ClickHouse compatibility
        auto pos = canonical_symbol.find(':');
        if (pos != std::string::npos) {
            try {
                snapshot.instrument_id = static_cast<uint32_t>(
                    std::stoul(canonical_symbol.substr(pos + 1)));
            } catch (...) {}
        }
    }
    
    // Forward to callback
    {
        std::lock_guard lock(callback_mutex_);
        if (snapshot_callback_) {
            snapshot_callback_(snapshot);
        }
    }
}

void KiteSubscriptionManager::handle_connect(size_t client_idx, size_t slot_idx) {
    std::cout << "[SubscriptionManager] Connected: client " << client_idx 
              << " slot " << slot_idx << "\n";
    
    // Re-send subscriptions for this slot
    std::shared_lock lock(pools_mutex_);
    if (client_idx >= client_pools_.size()) return;
    
    auto& slot = client_pools_[client_idx]->slots[slot_idx];
    if (!slot.subscribed_tokens.empty() && slot.ws) {
        std::vector<uint32_t> tokens(slot.subscribed_tokens.begin(),
                                      slot.subscribed_tokens.end());
        slot.ws->subscribe(tokens);
        slot.ws->set_mode(tokens, config_.default_mode);
        
        std::cout << "[SubscriptionManager] Re-subscribed " << tokens.size() 
                  << " tokens on reconnect\n";
    }
}

void KiteSubscriptionManager::handle_disconnect(
    size_t client_idx, size_t slot_idx, int code, const std::string& reason) {
    
    std::cout << "[SubscriptionManager] Disconnected: client " << client_idx 
              << " slot " << slot_idx << " (code: " << code << ", reason: " << reason << ")\n";
    
    // Mark subscriptions as not active
    std::unique_lock lock(subscriptions_mutex_);
    for (auto& [_, entry] : subscriptions_) {
        if (entry.client_index == client_idx && entry.ws_index == slot_idx) {
            entry.is_subscribed = false;
        }
    }
}

void KiteSubscriptionManager::handle_error(
    size_t client_idx, size_t slot_idx, const std::string& error) {
    
    std::cerr << "[SubscriptionManager] Error on client " << client_idx 
              << " slot " << slot_idx << ": " << error << "\n";
    
    // Forward to error callback
    std::lock_guard lock(callback_mutex_);
    if (error_callback_) {
        error_callback_("", error);  // No specific symbol
    }
}

KiteSubscriptionManager::SlotAssignment KiteSubscriptionManager::find_available_slot() const {
    // Find slot with most available capacity
    SlotAssignment best;
    size_t best_capacity = 0;
    
    std::shared_lock lock(pools_mutex_);
    for (size_t ci = 0; ci < client_pools_.size(); ++ci) {
        for (size_t si = 0; si < config_.max_ws_per_client; ++si) {
            const auto& slot = client_pools_[ci]->slots[si];
            size_t capacity = slot.available_capacity(config_.max_tokens_per_ws);
            
            if (capacity > best_capacity) {
                best_capacity = capacity;
                best.client_index = ci;
                best.slot_index = si;
                best.found = true;
            }
        }
    }
    
    return best;
}

bool KiteSubscriptionManager::apply_subscription(SubscriptionEntry& entry) {
    std::shared_lock lock(pools_mutex_);
    
    if (entry.client_index >= client_pools_.size()) {
        return false;
    }
    
    auto& pool = client_pools_[entry.client_index];
    auto& slot = pool->slots[entry.ws_index];
    
    if (!slot.ws) {
        return false;
    }
    
    // Add to slot's subscription set
    slot.subscribed_tokens.insert(entry.instrument_token);
    
    // If connected, send subscribe command
    if (slot.ws->is_connected()) {
        slot.ws->subscribe({entry.instrument_token});
        slot.ws->set_mode({entry.instrument_token}, config_.default_mode);
    } else if (running_) {
        // Connect the slot if not connected
        slot.ws->connect();
    }
    
    entry.is_subscribed = true;
    entry.subscribed_at = std::chrono::steady_clock::now();
    
    return true;
}

void KiteSubscriptionManager::apply_unsubscription(SubscriptionEntry& entry) {
    std::shared_lock lock(pools_mutex_);
    
    if (entry.client_index >= client_pools_.size()) {
        return;
    }
    
    auto& pool = client_pools_[entry.client_index];
    auto& slot = pool->slots[entry.ws_index];
    
    slot.subscribed_tokens.erase(entry.instrument_token);
    
    if (slot.ws && slot.ws->is_connected()) {
        slot.ws->unsubscribe({entry.instrument_token});
    }
    
    entry.is_subscribed = false;
}

std::optional<uint32_t> KiteSubscriptionManager::resolve_instrument_token(
    const std::string& canonical_symbol) const {
    
    if (!instrument_manager_) {
        return std::nullopt;
    }
    
    // Parse exchange_token from canonical symbol
    auto pos = canonical_symbol.find(':');
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    
    try {
        uint32_t exchange_token = static_cast<uint32_t>(
            std::stoul(canonical_symbol.substr(pos + 1)));
        
        // Look up instrument_token via InstrumentManager
        const auto* info = instrument_manager_->resolve(exchange_token);
        if (info) {
            return info->instrument_token;
        }
        
        // Also try canonical symbol lookup
        const auto* info2 = instrument_manager_->resolve_by_canonical(canonical_symbol);
        if (info2) {
            return info2->instrument_token;
        }
        
    } catch (...) {}
    
    return std::nullopt;
}

// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<KiteSubscriptionManager> create_subscription_manager(
    std::shared_ptr<core::InstrumentManager> instrument_manager) {
    
    return std::make_unique<KiteSubscriptionManager>(std::move(instrument_manager));
}

std::unique_ptr<KiteSubscriptionManager> create_subscription_manager(
    std::shared_ptr<core::InstrumentManager> instrument_manager,
    const SubscriptionManagerConfig& config) {
    
    return std::make_unique<KiteSubscriptionManager>(std::move(instrument_manager), config);
}

} // namespace payoff::kite
