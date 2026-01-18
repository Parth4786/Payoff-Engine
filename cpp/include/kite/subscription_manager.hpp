#pragma once
/**
 * @file subscription_manager.hpp
 * @brief Kite WebSocket Subscription Manager
 * 
 * Manages token subscriptions across multiple Kite WebSocket connections.
 * 
 * Key Features:
 * - Maps exchange_token (ClickHouse) ↔ instrument_token (Kite WS)
 * - Connection pooling: 3 WebSocket connections per client
 * - Token limit: 3000 tokens max per WebSocket connection
 * - Multi-credential support: Multiple Kite clients with different credentials
 * - Auto-redistribution when connections fail
 * 
 * Reference: docs/Market-observatory/data_sources/kite_ws_source.py
 *            docs/Market-observatory/streaming/hub.py
 * 
 * Kite Limits:
 * - Max 3 WebSocket connections per API key
 * - Max 3000 tokens per WebSocket connection
 * - Total max: 9000 tokens per API key (3 × 3000)
 */

#include "kite/kite_websocket.hpp"
#include "core/instrument_manager.hpp"
#include "core/models.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace payoff::kite {

// ============================================================================
// Configuration
// ============================================================================

struct KiteCredentials {
    std::string api_key;
    std::string access_token;
    std::string user_id;  // Optional: for logging/debugging
    
    [[nodiscard]] bool is_valid() const noexcept {
        return !api_key.empty() && !access_token.empty();
    }
};

struct SubscriptionManagerConfig {
    // Connection limits (Kite constraints)
    static constexpr size_t MAX_WS_PER_CLIENT = 3;
    static constexpr size_t MAX_TOKENS_PER_WS = 3000;
    
    // Behavior
    size_t max_ws_per_client = MAX_WS_PER_CLIENT;
    size_t max_tokens_per_ws = MAX_TOKENS_PER_WS;
    bool auto_redistribute = true;      // Redistribute tokens on connection failure
    bool auto_reconnect = true;
    int reconnect_delay_ms = 1000;
    int max_reconnect_retries = 5;
    
    // Mode
    WSMode default_mode = WSMode::Full;  // Need Full for OI + depth
};

// ============================================================================
// Subscription Entry - Tracks each subscribed instrument
// ============================================================================

struct SubscriptionEntry {
    std::string canonical_symbol;     // e.g., "NFO:49543"
    uint32_t exchange_token = 0;      // For ClickHouse lookups
    uint32_t instrument_token = 0;    // For Kite WS subscriptions
    
    // Connection assignment
    size_t client_index = 0;          // Which KiteCredentials
    size_t ws_index = 0;              // Which WebSocket (0-2)
    
    // Subscription state
    bool is_subscribed = false;
    std::chrono::steady_clock::time_point subscribed_at;
    
    // Last tick info
    std::chrono::steady_clock::time_point last_tick;
    size_t tick_count = 0;
};

// ============================================================================
// WebSocket Slot - One of 3 connections per client
// ============================================================================

struct WebSocketSlot {
    std::unique_ptr<KiteWebSocket> ws;
    std::unordered_set<uint32_t> subscribed_tokens;  // instrument_tokens
    
    ConnectionStatus status() const {
        return ws ? ws->status() : ConnectionStatus::Disconnected;
    }
    
    size_t token_count() const {
        return subscribed_tokens.size();
    }
    
    bool can_accept(size_t max_tokens) const {
        return token_count() < max_tokens;
    }
    
    size_t available_capacity(size_t max_tokens) const {
        return token_count() < max_tokens ? max_tokens - token_count() : 0;
    }
};

// ============================================================================
// Client Pool - All connections for one Kite credential
// ============================================================================

struct ClientPool {
    KiteCredentials credentials;
    std::array<WebSocketSlot, 3> slots;  // Max 3 WS per client
    std::atomic<bool> is_active{false};
    
    size_t total_subscribed() const {
        size_t total = 0;
        for (const auto& slot : slots) {
            total += slot.token_count();
        }
        return total;
    }
    
    size_t total_capacity(size_t max_per_ws) const {
        return slots.size() * max_per_ws;
    }
    
    size_t available_capacity(size_t max_per_ws) const {
        return total_capacity(max_per_ws) - total_subscribed();
    }
};

// ============================================================================
// Callbacks
// ============================================================================

using SubscriptionSnapshotCallback = std::function<void(const core::DepthSnapshot&)>;
using SubscriptionErrorCallback = std::function<void(const std::string& symbol, const std::string& error)>;
using SubscriptionStatusCallback = std::function<void(const std::string& symbol, bool subscribed)>;

// ============================================================================
// KiteSubscriptionManager - Central subscription orchestrator
// ============================================================================

class KiteSubscriptionManager {
public:
    explicit KiteSubscriptionManager(
        std::shared_ptr<core::InstrumentManager> instrument_manager,
        SubscriptionManagerConfig config = {});
    
    ~KiteSubscriptionManager();
    
    // Non-copyable
    KiteSubscriptionManager(const KiteSubscriptionManager&) = delete;
    KiteSubscriptionManager& operator=(const KiteSubscriptionManager&) = delete;
    
    // ========================================================================
    // Credential Management
    // ========================================================================
    
    /**
     * @brief Add Kite credentials for connection pooling
     * @return Index of added credentials (-1 if failed)
     */
    int add_credentials(const KiteCredentials& creds);
    
    /**
     * @brief Add credentials from environment/config
     * Uses KITE_API_KEY and KITE_ACCESS_TOKEN
     */
    int add_credentials_from_config();
    
    /**
     * @brief Remove credentials (disconnects associated connections)
     */
    void remove_credentials(size_t index);
    
    /**
     * @brief Get number of credential sets
     */
    [[nodiscard]] size_t credential_count() const noexcept;
    
    /**
     * @brief Get total capacity across all credentials
     */
    [[nodiscard]] size_t total_capacity() const noexcept;
    
    /**
     * @brief Get current subscription count
     */
    [[nodiscard]] size_t subscription_count() const noexcept;
    
    // ========================================================================
    // Subscription Management
    // ========================================================================
    
    /**
     * @brief Subscribe to a symbol (canonical format: "NFO:49543")
     * 
     * Resolves exchange_token → instrument_token using InstrumentManager,
     * then assigns to an available WebSocket slot.
     * 
     * @return true if subscription initiated
     */
    bool subscribe(const std::string& canonical_symbol);
    
    /**
     * @brief Subscribe to multiple symbols
     * @return Number of symbols successfully subscribed
     */
    size_t subscribe_batch(const std::vector<std::string>& canonical_symbols);
    
    /**
     * @brief Subscribe to an entire option chain
     * 
     * @param underlying Underlying symbol (e.g., "NIFTY")
     * @param expiry Optional expiry date filter
     * @return Number of instruments subscribed
     */
    size_t subscribe_option_chain(
        const std::string& underlying,
        std::optional<std::chrono::year_month_day> expiry = std::nullopt);
    
    /**
     * @brief Unsubscribe from a symbol
     */
    void unsubscribe(const std::string& canonical_symbol);
    
    /**
     * @brief Unsubscribe from multiple symbols
     */
    void unsubscribe_batch(const std::vector<std::string>& canonical_symbols);
    
    /**
     * @brief Unsubscribe from entire option chain
     */
    void unsubscribe_option_chain(
        const std::string& underlying,
        std::optional<std::chrono::year_month_day> expiry = std::nullopt);
    
    /**
     * @brief Replace all subscriptions (unsubscribe old, subscribe new)
     * 
     * Efficient for switching between option chains.
     * Minimizes unnecessary unsubscribe/subscribe operations.
     */
    void replace_subscriptions(const std::vector<std::string>& new_symbols);
    
    /**
     * @brief Clear all subscriptions
     */
    void unsubscribe_all();
    
    // ========================================================================
    // Query
    // ========================================================================
    
    /**
     * @brief Check if symbol is subscribed
     */
    [[nodiscard]] bool is_subscribed(const std::string& canonical_symbol) const;
    
    /**
     * @brief Get all subscribed symbols
     */
    [[nodiscard]] std::vector<std::string> get_subscribed_symbols() const;
    
    /**
     * @brief Get subscription entry for a symbol
     */
    [[nodiscard]] std::optional<SubscriptionEntry> get_subscription(
        const std::string& canonical_symbol) const;
    
    /**
     * @brief Get subscription statistics
     */
    struct Stats {
        size_t total_credentials = 0;
        size_t active_connections = 0;
        size_t total_subscribed = 0;
        size_t total_capacity = 0;
        size_t ticks_received = 0;
        std::chrono::steady_clock::time_point last_tick;
    };
    [[nodiscard]] Stats get_stats() const;
    
    // ========================================================================
    // Connection Management
    // ========================================================================
    
    /**
     * @brief Start all connections (call after adding credentials)
     */
    void start();
    
    /**
     * @brief Stop all connections
     */
    void stop();
    
    /**
     * @brief Check if running
     */
    [[nodiscard]] bool is_running() const noexcept;
    
    /**
     * @brief Redistribute tokens across connections
     * 
     * Call this after connection failure or credential changes.
     * Automatically called if auto_redistribute is enabled.
     */
    void redistribute_tokens();
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    /**
     * @brief Set callback for incoming snapshots
     */
    void on_snapshot(SubscriptionSnapshotCallback callback);
    
    /**
     * @brief Set callback for errors
     */
    void on_error(SubscriptionErrorCallback callback);
    
    /**
     * @brief Set callback for subscription status changes
     */
    void on_status(SubscriptionStatusCallback callback);

private:
    std::shared_ptr<core::InstrumentManager> instrument_manager_;
    SubscriptionManagerConfig config_;
    
    // Client pools (one per credential set)
    std::vector<std::unique_ptr<ClientPool>> client_pools_;
    mutable std::shared_mutex pools_mutex_;
    
    // Subscription tracking
    std::unordered_map<std::string, SubscriptionEntry> subscriptions_;  // canonical → entry
    std::unordered_map<uint32_t, std::string> instrument_to_canonical_; // instrument_token → canonical
    mutable std::shared_mutex subscriptions_mutex_;
    
    // State
    std::atomic<bool> running_{false};
    std::atomic<size_t> total_ticks_{0};
    std::chrono::steady_clock::time_point last_tick_time_;
    
    // Callbacks
    SubscriptionSnapshotCallback snapshot_callback_;
    SubscriptionErrorCallback error_callback_;
    SubscriptionStatusCallback status_callback_;
    std::mutex callback_mutex_;
    
    // Internal methods
    void setup_websocket_callbacks(ClientPool& pool, size_t slot_index);
    void handle_tick(const KiteTick& tick);
    void handle_connect(size_t client_idx, size_t slot_idx);
    void handle_disconnect(size_t client_idx, size_t slot_idx, int code, const std::string& reason);
    void handle_error(size_t client_idx, size_t slot_idx, const std::string& error);
    
    // Find best slot for new subscription
    struct SlotAssignment {
        size_t client_index = 0;
        size_t slot_index = 0;
        bool found = false;
    };
    SlotAssignment find_available_slot() const;
    
    // Apply subscription to WebSocket
    bool apply_subscription(SubscriptionEntry& entry);
    void apply_unsubscription(SubscriptionEntry& entry);
    
    // Token resolution
    std::optional<uint32_t> resolve_instrument_token(const std::string& canonical_symbol) const;
};

// ============================================================================
// Factory
// ============================================================================

/**
 * @brief Create subscription manager with default config
 */
std::unique_ptr<KiteSubscriptionManager> create_subscription_manager(
    std::shared_ptr<core::InstrumentManager> instrument_manager);

/**
 * @brief Create subscription manager with custom config
 */
std::unique_ptr<KiteSubscriptionManager> create_subscription_manager(
    std::shared_ptr<core::InstrumentManager> instrument_manager,
    const SubscriptionManagerConfig& config);

} // namespace payoff::kite
