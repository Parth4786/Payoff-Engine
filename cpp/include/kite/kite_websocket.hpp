#pragma once
/**
 * @file kite_websocket.hpp
 * @brief Kite WebSocket client for live tick streaming
 * 
 * Implements:
 * - Connection management with auto-reconnect
 * - Mode switching (ltp, quote, full)
 * - Tick parsing and normalization
 * - Subscription management
 * 
 * Reference: docs/DeskMetrics/inspiration/pykiteconnect-master
 */

#include "core/models.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace payoff::kite {

// ============================================================================
// WebSocket Mode
// ============================================================================

enum class WSMode : uint8_t {
    LTP = 1,       // Only last traded price
    Quote = 2,     // Quote data (OHLC, volume, OI)
    Full = 3       // Full depth (5 levels bid/ask)
};

constexpr const char* ws_mode_to_string(WSMode mode) {
    switch (mode) {
        case WSMode::LTP: return "ltp";
        case WSMode::Quote: return "quote";
        case WSMode::Full: return "full";
        default: return "unknown";
    }
}

// ============================================================================
// Tick Data (raw from WebSocket)
// ============================================================================

struct KiteTick {
    uint32_t instrument_token = 0;
    WSMode mode = WSMode::LTP;
    
    // LTP mode
    double last_price = 0.0;
    
    // Quote mode (additional)
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    int64_t volume = 0;
    int64_t buy_quantity = 0;
    int64_t sell_quantity = 0;
    int64_t oi = 0;
    int64_t oi_day_high = 0;
    int64_t oi_day_low = 0;
    
    // Full mode (additional)
    struct DepthLevel {
        double price = 0.0;
        int64_t quantity = 0;
        int32_t orders = 0;
    };
    
    std::vector<DepthLevel> bids;  // Up to 5 levels
    std::vector<DepthLevel> asks;  // Up to 5 levels
    
    // Timestamps
    std::chrono::system_clock::time_point exchange_timestamp;
    std::chrono::system_clock::time_point last_trade_time;
    
    /**
     * @brief Convert to canonical DepthSnapshot
     */
    [[nodiscard]] core::DepthSnapshot to_snapshot() const;
};

// ============================================================================
// Callbacks
// ============================================================================

using TickCallback = std::function<void(const KiteTick&)>;
using SnapshotCallback = std::function<void(const core::DepthSnapshot&)>;
using ConnectCallback = std::function<void()>;
using DisconnectCallback = std::function<void(int code, const std::string& reason)>;
using ErrorCallback = std::function<void(const std::string& error)>;

// ============================================================================
// Connection Status
// ============================================================================

enum class ConnectionStatus : uint8_t {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    Reconnecting = 3
};

// ============================================================================
// KiteWebSocket - Live tick streaming
// ============================================================================

class KiteWebSocket {
public:
    /**
     * @brief Construct with API key and access token
     */
    KiteWebSocket(const std::string& api_key, const std::string& access_token);
    
    /**
     * @brief Construct from config
     */
    KiteWebSocket();
    
    ~KiteWebSocket();
    
    // Non-copyable
    KiteWebSocket(const KiteWebSocket&) = delete;
    KiteWebSocket& operator=(const KiteWebSocket&) = delete;
    
    // ========================================================================
    // Connection Management
    // ========================================================================
    
    /**
     * @brief Connect to WebSocket server
     * @return true if connection initiated
     */
    bool connect();
    
    /**
     * @brief Disconnect from server
     */
    void disconnect();
    
    /**
     * @brief Check connection status
     */
    [[nodiscard]] ConnectionStatus status() const noexcept;
    
    /**
     * @brief Check if connected
     */
    [[nodiscard]] bool is_connected() const noexcept;
    
    /**
     * @brief Set auto-reconnect behavior
     */
    void set_auto_reconnect(bool enabled, int max_retries = 5, int delay_ms = 1000);
    
    // ========================================================================
    // Subscription Management
    // ========================================================================
    
    /**
     * @brief Subscribe to instruments
     * @param instrument_tokens List of Kite instrument tokens
     */
    void subscribe(const std::vector<uint32_t>& instrument_tokens);
    
    /**
     * @brief Unsubscribe from instruments
     */
    void unsubscribe(const std::vector<uint32_t>& instrument_tokens);
    
    /**
     * @brief Set mode for instruments
     * @param instrument_tokens Tokens to set mode for
     * @param mode LTP, Quote, or Full
     */
    void set_mode(const std::vector<uint32_t>& instrument_tokens, WSMode mode);
    
    /**
     * @brief Get currently subscribed tokens
     */
    [[nodiscard]] std::vector<uint32_t> get_subscribed() const;
    
    /**
     * @brief Get subscription count
     */
    [[nodiscard]] size_t subscription_count() const noexcept;
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    /**
     * @brief Set tick callback (raw ticks)
     */
    void on_ticks(TickCallback callback);
    
    /**
     * @brief Set snapshot callback (normalized snapshots)
     */
    void on_snapshots(SnapshotCallback callback);
    
    /**
     * @brief Set connect callback
     */
    void on_connect(ConnectCallback callback);
    
    /**
     * @brief Set disconnect callback
     */
    void on_disconnect(DisconnectCallback callback);
    
    /**
     * @brief Set error callback
     */
    void on_error(ErrorCallback callback);
    
    // ========================================================================
    // Getters
    // ========================================================================
    
    [[nodiscard]] const std::string& api_key() const noexcept { return api_key_; }

private:
    std::string api_key_;
    std::string access_token_;
    
    // Connection state
    std::atomic<ConnectionStatus> status_{ConnectionStatus::Disconnected};
    std::atomic<bool> running_{false};
    std::atomic<bool> auto_reconnect_{true};
    int max_retries_ = 5;
    int reconnect_delay_ms_ = 1000;
    
    // Subscriptions
    std::unordered_set<uint32_t> subscriptions_;
    std::unordered_map<uint32_t, WSMode> modes_;
    mutable std::mutex subscription_mutex_;
    
    // Callbacks
    TickCallback tick_callback_;
    SnapshotCallback snapshot_callback_;
    ConnectCallback connect_callback_;
    DisconnectCallback disconnect_callback_;
    ErrorCallback error_callback_;
    std::mutex callback_mutex_;
    
    // Threading
    std::unique_ptr<std::thread> ws_thread_;
    
    // Internal methods
    void ws_loop();
    void process_message(const std::vector<uint8_t>& data);
    KiteTick parse_binary_tick(const uint8_t* data, size_t len, WSMode mode);
    void notify_tick(const KiteTick& tick);
    void notify_connect();
    void notify_disconnect(int code, const std::string& reason);
    void notify_error(const std::string& error);
    
    // WinHTTP WebSocket helpers
    static std::wstring string_to_wstring(const std::string& str);
    void handle_reconnect(int& retry_count);
    void send_pending_subscriptions(void* hWebSocket);
    
    // Constants
    static constexpr const char* WS_URL = "wss://ws.kite.trade";
};

// ============================================================================
// Factory
// ============================================================================

/**
 * @brief Create KiteWebSocket from environment/config
 */
std::unique_ptr<KiteWebSocket> create_kite_websocket();

/**
 * @brief Create KiteWebSocket with existing client auth
 */
std::unique_ptr<KiteWebSocket> create_kite_websocket(
    const std::string& api_key,
    const std::string& access_token);

} // namespace payoff::kite
