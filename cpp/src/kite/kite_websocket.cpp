/**
 * @file kite_websocket.cpp
 * @brief Kite WebSocket client implementation
 * 
 * Handles binary tick parsing from Kite's WebSocket feed.
 * Protocol: wss://ws.kite.trade?api_key=xxx&access_token=xxx
 * 
 * Uses WinHTTP WebSocket API for REAL WebSocket connections on Windows.
 */

#include "kite/kite_websocket.hpp"
#include "core/config.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

#ifdef _WIN32
    #include <windows.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
#endif

namespace payoff::kite {

// ============================================================================
// Binary Protocol Constants
// ============================================================================

namespace {

// Packet types
constexpr uint8_t PACKET_LTP = 1;
constexpr uint8_t PACKET_QUOTE = 2;  
constexpr uint8_t PACKET_FULL = 3;

// Packet sizes
constexpr size_t LTP_PACKET_SIZE = 8;      // token(4) + ltp(4)
constexpr size_t QUOTE_PACKET_SIZE = 44;   // LTP + OHLC + vol + OI
constexpr size_t FULL_PACKET_SIZE = 184;   // Quote + depth (5x5)

// Helper functions for binary parsing (big-endian)
inline uint32_t read_uint32_be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

inline int32_t read_int32_be(const uint8_t* data) {
    return static_cast<int32_t>(read_uint32_be(data));
}

inline uint64_t read_uint64_be(const uint8_t* data) {
    return (static_cast<uint64_t>(read_uint32_be(data)) << 32) |
           static_cast<uint64_t>(read_uint32_be(data + 4));
}

// Price divisor (prices come as integers, divide by 100)
constexpr double PRICE_DIVISOR = 100.0;

} // anonymous namespace

// ============================================================================
// KiteTick -> DepthSnapshot conversion
// ============================================================================

core::DepthSnapshot KiteTick::to_snapshot() const {
    core::DepthSnapshot snap;
    
    snap.instrument_id = instrument_token;
    snap.symbol = std::to_string(instrument_token);
    snap.source = core::Source::KiteWS;
    
    // Exchange timestamp
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        exchange_timestamp.time_since_epoch());
    snap.exchange_timestamp = ms;
    
    // Receive timestamp (now)
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    snap.receive_timestamp = now_ms;
    
    // Trade info
    snap.trade.last_price = last_price;
    snap.trade.last_traded_quantity = 0;  // Not in tick
    snap.trade.total_traded_quantity = volume;
    snap.trade.total_buy_quantity = buy_quantity;
    snap.trade.total_sell_quantity = sell_quantity;
    snap.trade.open_interest = oi;
    snap.trade.open = open;
    snap.trade.high = high;
    snap.trade.low = low;
    snap.trade.close = close;
    
    // Depth levels
    for (const auto& level : bids) {
        snap.bids.push_back({level.price, level.quantity, level.orders});
    }
    for (const auto& level : asks) {
        snap.asks.push_back({level.price, level.quantity, level.orders});
    }
    
    snap.is_partial = (mode != WSMode::Full);
    
    return snap;
}

// ============================================================================
// KiteWebSocket Implementation
// ============================================================================

KiteWebSocket::KiteWebSocket(const std::string& api_key, const std::string& access_token)
    : api_key_(api_key), access_token_(access_token) {
}

KiteWebSocket::KiteWebSocket() {
    auto& cfg = config::config();
    if (!cfg.is_loaded()) {
        cfg.load();
    }
    api_key_ = cfg.kite_api_key();
    access_token_ = cfg.kite_access_token();
}

KiteWebSocket::~KiteWebSocket() {
    disconnect();
}

bool KiteWebSocket::connect() {
    if (status_ != ConnectionStatus::Disconnected) {
        return false;
    }
    
    if (api_key_.empty() || access_token_.empty()) {
        notify_error("Missing API key or access token");
        return false;
    }
    
    status_ = ConnectionStatus::Connecting;
    running_ = true;
    
    // Start WebSocket thread
    ws_thread_ = std::make_unique<std::thread>(&KiteWebSocket::ws_loop, this);
    
    return true;
}

void KiteWebSocket::disconnect() {
    running_ = false;
    
    if (ws_thread_ && ws_thread_->joinable()) {
        ws_thread_->join();
    }
    
    status_ = ConnectionStatus::Disconnected;
}

ConnectionStatus KiteWebSocket::status() const noexcept {
    return status_.load();
}

bool KiteWebSocket::is_connected() const noexcept {
    return status_.load() == ConnectionStatus::Connected;
}

void KiteWebSocket::set_auto_reconnect(bool enabled, int max_retries, int delay_ms) {
    auto_reconnect_ = enabled;
    max_retries_ = max_retries;
    reconnect_delay_ms_ = delay_ms;
}

void KiteWebSocket::subscribe(const std::vector<uint32_t>& instrument_tokens) {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    
    for (uint32_t token : instrument_tokens) {
        subscriptions_.insert(token);
        if (modes_.find(token) == modes_.end()) {
            modes_[token] = WSMode::Full;  // Default to full mode
        }
    }
    
    // Send subscribe message if connected
    if (is_connected()) {
        // Would send: {"a": "subscribe", "v": [token1, token2, ...]}
    }
}

void KiteWebSocket::unsubscribe(const std::vector<uint32_t>& instrument_tokens) {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    
    for (uint32_t token : instrument_tokens) {
        subscriptions_.erase(token);
        modes_.erase(token);
    }
    
    // Send unsubscribe message if connected
    if (is_connected()) {
        // Would send: {"a": "unsubscribe", "v": [token1, token2, ...]}
    }
}

void KiteWebSocket::set_mode(const std::vector<uint32_t>& instrument_tokens, WSMode mode) {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    
    for (uint32_t token : instrument_tokens) {
        modes_[token] = mode;
    }
    
    // Send mode message if connected
    if (is_connected()) {
        // Would send: {"a": "mode", "v": ["full", [token1, token2, ...]]}
    }
}

std::vector<uint32_t> KiteWebSocket::get_subscribed() const {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    return std::vector<uint32_t>(subscriptions_.begin(), subscriptions_.end());
}

size_t KiteWebSocket::subscription_count() const noexcept {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    return subscriptions_.size();
}

void KiteWebSocket::on_ticks(TickCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    tick_callback_ = std::move(callback);
}

void KiteWebSocket::on_snapshots(SnapshotCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    snapshot_callback_ = std::move(callback);
}

void KiteWebSocket::on_connect(ConnectCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    connect_callback_ = std::move(callback);
}

void KiteWebSocket::on_disconnect(DisconnectCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    disconnect_callback_ = std::move(callback);
}

void KiteWebSocket::on_error(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    error_callback_ = std::move(callback);
}

void KiteWebSocket::ws_loop() {
    // REAL WebSocket connection using WinHTTP
    // Protocol: wss://ws.kite.trade?api_key=xxx&access_token=xxx
    
#ifdef _WIN32
    int retry_count = 0;
    
    while (running_) {
        // Initialize WinHTTP session
        HINTERNET hSession = WinHttpOpen(
            L"Payoff-Engine/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        
        if (!hSession) {
            notify_error("Failed to create WinHTTP session");
            handle_reconnect(retry_count);
            continue;
        }
        
        // Connect to ws.kite.trade (port 443 for WSS)
        HINTERNET hConnect = WinHttpConnect(
            hSession,
            L"ws.kite.trade",
            INTERNET_DEFAULT_HTTPS_PORT,
            0);
        
        if (!hConnect) {
            notify_error("Failed to connect to ws.kite.trade");
            WinHttpCloseHandle(hSession);
            handle_reconnect(retry_count);
            continue;
        }
        
        // Build query path: /?api_key=xxx&access_token=xxx
        std::wstring path = L"/?api_key=" + string_to_wstring(api_key_) + 
                           L"&access_token=" + string_to_wstring(access_token_);
        
        // Open WebSocket request
        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect,
            L"GET",
            path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        
        if (!hRequest) {
            notify_error("Failed to create WebSocket request");
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            handle_reconnect(retry_count);
            continue;
        }
        
        // Set WebSocket upgrade option
        BOOL opt = TRUE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, &opt, sizeof(opt));
        
        // Send request
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            notify_error("Failed to send WebSocket upgrade request");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            handle_reconnect(retry_count);
            continue;
        }
        
        // Receive response
        if (!WinHttpReceiveResponse(hRequest, nullptr)) {
            notify_error("Failed to receive WebSocket upgrade response");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            handle_reconnect(retry_count);
            continue;
        }
        
        // Complete WebSocket upgrade
        HINTERNET hWebSocket = WinHttpWebSocketCompleteUpgrade(hRequest, 0);
        if (!hWebSocket) {
            DWORD err = GetLastError();
            std::string err_msg = "WebSocket upgrade failed: " + std::to_string(err);
            notify_error(err_msg);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            handle_reconnect(retry_count);
            continue;
        }
        
        // Connection successful!
        WinHttpCloseHandle(hRequest);  // No longer needed after upgrade
        status_ = ConnectionStatus::Connected;
        notify_connect();
        retry_count = 0;  // Reset retry count on successful connection
        
        // Send subscriptions if any
        send_pending_subscriptions(hWebSocket);
        
        // Main receive loop
        std::vector<uint8_t> buffer(8192);
        std::vector<uint8_t> accumulated;
        
        while (running_ && is_connected()) {
            DWORD bytes_read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE buffer_type;
            
            DWORD result = WinHttpWebSocketReceive(
                hWebSocket,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytes_read,
                &buffer_type);
            
            if (result != ERROR_SUCCESS) {
                if (result == ERROR_WINHTTP_OPERATION_CANCELLED) {
                    // Clean shutdown
                    break;
                }
                notify_error("WebSocket receive error: " + std::to_string(result));
                break;
            }
            
            // Accumulate data
            accumulated.insert(accumulated.end(), buffer.begin(), buffer.begin() + bytes_read);
            
            // Process complete messages
            if (buffer_type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                buffer_type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) {
                
                // Binary data from Kite
                if (!accumulated.empty()) {
                    process_message(accumulated);
                    accumulated.clear();
                }
            } else if (buffer_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                // Server initiated close
                break;
            }
        }
        
        // Clean up WebSocket
        WinHttpWebSocketClose(hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        WinHttpCloseHandle(hWebSocket);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        // Handle disconnection
        status_ = ConnectionStatus::Disconnected;
        notify_disconnect(0, "Connection closed");
        
        // Reconnect logic
        if (running_ && auto_reconnect_ && retry_count < max_retries_) {
            handle_reconnect(retry_count);
        } else {
            break;
        }
    }
    
#else
    // Non-Windows: stub (would need libwebsockets or Beast)
    notify_error("WebSocket not supported on this platform");
#endif
    
    status_ = ConnectionStatus::Disconnected;
}

// Helper: Convert std::string to std::wstring
std::wstring KiteWebSocket::string_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

// Helper: Handle reconnection delay
void KiteWebSocket::handle_reconnect(int& retry_count) {
    if (!running_ || !auto_reconnect_) return;
    
    status_ = ConnectionStatus::Reconnecting;
    retry_count++;
    
    // Exponential backoff with jitter
    int delay = reconnect_delay_ms_ * (1 << std::min(retry_count - 1, 5));
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
}

// Send pending subscriptions after connect
void KiteWebSocket::send_pending_subscriptions([[maybe_unused]] void* hWebSocket) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    
    if (subscriptions_.empty()) return;
    
    // Build subscribe message: {"a": "subscribe", "v": [token1, token2, ...]}
    std::ostringstream json;
    json << "{\"a\":\"subscribe\",\"v\":[";
    bool first = true;
    for (uint32_t token : subscriptions_) {
        if (!first) json << ",";
        json << token;
        first = false;
    }
    json << "]}";
    
    std::string msg = json.str();
    WinHttpWebSocketSend(
        static_cast<HINTERNET>(hWebSocket),
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<void*>(static_cast<const void*>(msg.c_str())),
        static_cast<DWORD>(msg.length()));
    
    // Also send mode message if not all LTP
    // {"a": "mode", "v": ["full", [token1, token2, ...]]}
    std::ostringstream mode_json;
    mode_json << "{\"a\":\"mode\",\"v\":[\"full\",[";
    first = true;
    for (uint32_t token : subscriptions_) {
        if (!first) mode_json << ",";
        mode_json << token;
        first = false;
    }
    mode_json << "]]}";
    
    std::string mode_msg = mode_json.str();
    WinHttpWebSocketSend(
        static_cast<HINTERNET>(hWebSocket),
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<void*>(static_cast<const void*>(mode_msg.c_str())),
        static_cast<DWORD>(mode_msg.length()));
#endif
}

void KiteWebSocket::process_message(const std::vector<uint8_t>& data) {
    if (data.size() < 2) return;
    
    // First 2 bytes: number of packets
    uint16_t num_packets = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    
    size_t offset = 2;
    
    for (uint16_t i = 0; i < num_packets && offset < data.size(); ++i) {
        // Next 2 bytes: packet length
        if (offset + 2 > data.size()) break;
        
        uint16_t packet_len = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
        offset += 2;
        
        if (offset + packet_len > data.size()) break;
        
        // Determine mode based on packet length
        WSMode mode;
        if (packet_len == LTP_PACKET_SIZE) {
            mode = WSMode::LTP;
        } else if (packet_len == QUOTE_PACKET_SIZE) {
            mode = WSMode::Quote;
        } else if (packet_len == FULL_PACKET_SIZE) {
            mode = WSMode::Full;
        } else {
            // Unknown packet size, skip
            offset += packet_len;
            continue;
        }
        
        // Parse tick
        KiteTick tick = parse_binary_tick(&data[offset], packet_len, mode);
        notify_tick(tick);
        
        offset += packet_len;
    }
}

KiteTick KiteWebSocket::parse_binary_tick(const uint8_t* data, size_t len, WSMode mode) {
    KiteTick tick;
    tick.mode = mode;
    
    if (len < LTP_PACKET_SIZE) return tick;
    
    // Common fields
    tick.instrument_token = read_uint32_be(data);
    tick.last_price = static_cast<double>(read_int32_be(data + 4)) / PRICE_DIVISOR;
    
    if (mode == WSMode::LTP || len < QUOTE_PACKET_SIZE) {
        return tick;
    }
    
    // Quote fields (offsets from Kite documentation)
    tick.high = static_cast<double>(read_int32_be(data + 8)) / PRICE_DIVISOR;
    tick.low = static_cast<double>(read_int32_be(data + 12)) / PRICE_DIVISOR;
    tick.open = static_cast<double>(read_int32_be(data + 16)) / PRICE_DIVISOR;
    tick.close = static_cast<double>(read_int32_be(data + 20)) / PRICE_DIVISOR;
    
    // Volume and OI (prices are also divisible but these are quantities)
    tick.volume = static_cast<int64_t>(read_uint32_be(data + 24));
    tick.buy_quantity = static_cast<int64_t>(read_uint32_be(data + 28));
    tick.sell_quantity = static_cast<int64_t>(read_uint32_be(data + 32));
    tick.oi = static_cast<int64_t>(read_uint32_be(data + 36));
    
    // Timestamp (exchange timestamp in epoch seconds)
    uint32_t timestamp_sec = read_uint32_be(data + 40);
    tick.exchange_timestamp = std::chrono::system_clock::from_time_t(
        static_cast<time_t>(timestamp_sec));
    
    if (mode == WSMode::Quote || len < FULL_PACKET_SIZE) {
        return tick;
    }
    
    // Full mode - parse depth (5 levels each side)
    // Depth starts at offset 44
    // Each level: 12 bytes (quantity: 4, price: 4, orders: 4)
    
    size_t depth_offset = 44;
    
    // 5 buy levels
    for (int i = 0; i < 5 && depth_offset + 12 <= len; ++i) {
        KiteTick::DepthLevel level;
        level.quantity = static_cast<int64_t>(read_uint32_be(data + depth_offset));
        level.price = static_cast<double>(read_int32_be(data + depth_offset + 4)) / PRICE_DIVISOR;
        level.orders = static_cast<int32_t>(read_uint32_be(data + depth_offset + 8));
        tick.bids.push_back(level);
        depth_offset += 12;
    }
    
    // 5 sell levels
    for (int i = 0; i < 5 && depth_offset + 12 <= len; ++i) {
        KiteTick::DepthLevel level;
        level.quantity = static_cast<int64_t>(read_uint32_be(data + depth_offset));
        level.price = static_cast<double>(read_int32_be(data + depth_offset + 4)) / PRICE_DIVISOR;
        level.orders = static_cast<int32_t>(read_uint32_be(data + depth_offset + 8));
        tick.asks.push_back(level);
        depth_offset += 12;
    }
    
    return tick;
}

void KiteWebSocket::notify_tick(const KiteTick& tick) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    
    if (tick_callback_) {
        tick_callback_(tick);
    }
    
    if (snapshot_callback_) {
        snapshot_callback_(tick.to_snapshot());
    }
}

void KiteWebSocket::notify_connect() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (connect_callback_) {
        connect_callback_();
    }
}

void KiteWebSocket::notify_disconnect(int code, const std::string& reason) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (disconnect_callback_) {
        disconnect_callback_(code, reason);
    }
}

void KiteWebSocket::notify_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (error_callback_) {
        error_callback_(error);
    }
}

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<KiteWebSocket> create_kite_websocket() {
    return std::make_unique<KiteWebSocket>();
}

std::unique_ptr<KiteWebSocket> create_kite_websocket(
    const std::string& api_key,
    const std::string& access_token) {
    return std::make_unique<KiteWebSocket>(api_key, access_token);
}

} // namespace payoff::kite
