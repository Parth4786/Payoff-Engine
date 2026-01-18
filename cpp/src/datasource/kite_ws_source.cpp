/**
 * @file kite_ws_source.cpp
 * @brief Kite WebSocket data source adapter
 * 
 * Implements MarketDataSource interface for live Kite WebSocket streaming.
 * 
 * Key characteristics:
 * - Replay NOT supported (use ClickHouse for historical)
 * - Provides OI data (only source for open_interest, oi_day_high, oi_day_low)
 * - Requires instrument_token → exchange_token mapping via InstrumentManager
 * 
 * Reference: docs/Market-observatory/data_sources/kite_ws_source.py
 */

#include "core/datasource.hpp"
#include "core/config.hpp"
#include "core/instrument_manager.hpp"
#include "kite/kite_websocket.hpp"

#include <atomic>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace payoff::core {

// ============================================================================
// KiteWSDataSource - Live market data from Kite WebSocket
// ============================================================================

class KiteWSDataSource : public MarketDataSource {
public:
    KiteWSDataSource(const std::string& api_key, const std::string& access_token)
        : ws_(api_key, access_token), instrument_manager_(nullptr) {
    }
    
    KiteWSDataSource()
        : ws_(), instrument_manager_(nullptr) {
    }
    
    ~KiteWSDataSource() {
        stop_streaming();
    }
    
    /**
     * @brief Set instrument manager for token resolution
     */
    void set_instrument_manager(std::shared_ptr<InstrumentManager> mgr) {
        instrument_manager_ = std::move(mgr);
    }
    
    Source get_source_type() const noexcept override {
        return Source::KiteWS;
    }
    
    std::string get_name() const override {
        return "KiteWebSocket";
    }
    
    bool is_connected() const noexcept override {
        return ws_.is_connected();
    }
    
    std::vector<std::string> get_symbols() const override {
        // Return currently subscribed symbols as canonical format
        std::lock_guard<std::mutex> lock(subscription_mutex_);
        std::vector<std::string> result;
        result.reserve(subscribed_symbols_.size());
        for (const auto& sym : subscribed_symbols_) {
            result.push_back(sym);
        }
        return result;
    }
    
    bool subscribe(const std::vector<std::string>& symbols) override {
        std::vector<uint32_t> tokens;
        
        {
            std::lock_guard<std::mutex> lock(subscription_mutex_);
            for (const auto& sym : symbols) {
                subscribed_symbols_.insert(sym);
                
                // Resolve canonical symbol to instrument_token
                if (instrument_manager_) {
                    // Parse "NFO:12345" → exchange_token 12345
                    auto pos = sym.find(':');
                    if (pos != std::string::npos) {
                        try {
                            uint32_t exchange_token = static_cast<uint32_t>(
                                std::stoul(sym.substr(pos + 1)));
                            
                            // Look up instrument_token from exchange_token
                            if (auto* inst = instrument_manager_->resolve(exchange_token)) {
                                tokens.push_back(inst->instrument_token);
                                token_to_canonical_[inst->instrument_token] = sym;
                            }
                        } catch (...) {}
                    }
                }
            }
        }
        
        if (!tokens.empty()) {
            ws_.subscribe(tokens);
            ws_.set_mode(tokens, kite::WSMode::Full);  // Need FULL for OI + depth
        }
        
        return true;
    }
    
    void unsubscribe(const std::vector<std::string>& symbols) override {
        std::vector<uint32_t> tokens;
        
        {
            std::lock_guard<std::mutex> lock(subscription_mutex_);
            for (const auto& sym : symbols) {
                subscribed_symbols_.erase(sym);
                
                // Resolve to instrument_token
                if (instrument_manager_) {
                    auto pos = sym.find(':');
                    if (pos != std::string::npos) {
                        try {
                            uint32_t exchange_token = static_cast<uint32_t>(
                                std::stoul(sym.substr(pos + 1)));
                            
                            if (auto* inst = instrument_manager_->resolve(exchange_token)) {
                                tokens.push_back(inst->instrument_token);
                                token_to_canonical_.erase(inst->instrument_token);
                            }
                        } catch (...) {}
                    }
                }
            }
        }
        
        if (!tokens.empty()) {
            ws_.unsubscribe(tokens);
        }
    }
    
    /**
     * @brief Replay NOT supported - use ClickHouse for historical data
     */
    size_t replay_snapshots(
        const std::vector<std::string>& /* symbols */,
        const TimeRange& /* range */,
        SnapshotCallback /* callback */) override {
        
        // Kite WebSocket is live-only, no replay capability
        return 0;
    }
    
    void start_streaming(
        SnapshotCallback callback,
        ErrorCallback error_callback) override {
        
        if (streaming_) {
            return;
        }
        
        streaming_ = true;
        snapshot_callback_ = callback;
        error_callback_ = error_callback;
        
        // Set up tick callback to convert and forward snapshots
        ws_.on_ticks([this](const kite::KiteTick& tick) {
            if (!streaming_ || !snapshot_callback_) return;
            
            // Convert tick to snapshot
            DepthSnapshot snap = tick.to_snapshot();
            
            // Resolve canonical symbol using instrument_token
            {
                std::lock_guard<std::mutex> lock(subscription_mutex_);
                auto it = token_to_canonical_.find(tick.instrument_token);
                if (it != token_to_canonical_.end()) {
                    snap.symbol = it->second;
                    
                    // Also set instrument_id to exchange_token
                    auto pos = it->second.find(':');
                    if (pos != std::string::npos) {
                        try {
                            snap.instrument_id = static_cast<uint32_t>(
                                std::stoul(it->second.substr(pos + 1)));
                        } catch (...) {}
                    }
                }
            }
            
            // Forward to callback
            snapshot_callback_(snap);
        });
        
        // Set up error callback
        ws_.on_error([this](const std::string& error) {
            if (error_callback_) {
                error_callback_(error);
            }
        });
        
        // Connect
        ws_.connect();
    }
    
    void stop_streaming() override {
        streaming_ = false;
        ws_.disconnect();
    }
    
    bool is_streaming() const noexcept override {
        return streaming_ && ws_.is_connected();
    }
    
    /**
     * @brief Access underlying WebSocket for direct control
     */
    kite::KiteWebSocket& websocket() { return ws_; }
    const kite::KiteWebSocket& websocket() const { return ws_; }

private:
    kite::KiteWebSocket ws_;
    std::shared_ptr<InstrumentManager> instrument_manager_;
    
    std::atomic<bool> streaming_{false};
    SnapshotCallback snapshot_callback_;
    ErrorCallback error_callback_;
    
    std::set<std::string> subscribed_symbols_;
    std::unordered_map<uint32_t, std::string> token_to_canonical_;  // instrument_token → canonical
    mutable std::mutex subscription_mutex_;
};

// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<MarketDataSource> create_kite_ws_source() {
    return std::make_unique<KiteWSDataSource>();
}

std::unique_ptr<MarketDataSource> create_kite_ws_source(
    const std::string& api_key,
    const std::string& access_token) {
    
    return std::make_unique<KiteWSDataSource>(api_key, access_token);
}

} // namespace payoff::core