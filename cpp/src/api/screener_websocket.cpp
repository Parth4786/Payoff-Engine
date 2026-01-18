/**
 * @file screener_websocket.cpp
 * @brief WebSocket handlers for screener streaming replay
 * 
 * Protocol:
 * - Client sends: {"action": "start_replay", "instrument_ids": [...], "start": ms, "end": ms}
 * - Server streams: {"type": "replay_tick", ...} for each snapshot
 * - Server sends: {"type": "replay_complete", ...} when done
 * - Client sends: {"action": "stop_replay"} to cancel
 */

#include "screener/screener_service.hpp"
#include "core/config.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace payoff::api {

// Forward declaration from websocket_server.cpp
// In production, would be in a header

// ============================================================================
// Screener WebSocket Session
// ============================================================================

class ScreenerWSSession {
public:
    using SendCallback = std::function<void(const std::string&)>;
    
    ScreenerWSSession(
        const std::string& client_id,
        SendCallback send_callback,
        std::shared_ptr<screener::ScreenerService> screener)
        : client_id_(client_id)
        , send_callback_(std::move(send_callback))
        , screener_(std::move(screener)) {}
    
    ~ScreenerWSSession() {
        stop_replay();
    }
    
    // ========================================================================
    // Message Handlers
    // ========================================================================
    
    void handle_message(const std::string& message) {
        // Parse action
        auto action_pos = message.find("\"action\"");
        if (action_pos == std::string::npos) return;
        
        auto colon = message.find(':', action_pos);
        auto quote1 = message.find('"', colon);
        auto quote2 = message.find('"', quote1 + 1);
        
        if (quote1 == std::string::npos || quote2 == std::string::npos) return;
        
        std::string action = message.substr(quote1 + 1, quote2 - quote1 - 1);
        
        if (action == "start_replay") {
            handle_start_replay(message);
        } else if (action == "stop_replay") {
            stop_replay();
        } else if (action == "subscribe_market") {
            handle_subscribe_market(message);
        } else if (action == "unsubscribe_market") {
            handle_unsubscribe_market();
        }
    }
    
private:
    std::string client_id_;
    SendCallback send_callback_;
    std::shared_ptr<screener::ScreenerService> screener_;
    
    std::atomic<bool> replay_running_{false};
    std::unique_ptr<std::thread> replay_thread_;
    
    std::atomic<bool> market_subscribed_{false};
    std::unique_ptr<std::thread> market_thread_;
    
    // ========================================================================
    // Replay Streaming
    // ========================================================================
    
    void handle_start_replay(const std::string& message) {
        if (replay_running_) {
            send_error("Replay already running. Stop first.");
            return;
        }
        
        // Parse request
        screener::ReplayRequest request;
        
        // Parse instrument_ids
        auto ids_start = message.find("\"instrument_ids\"");
        if (ids_start != std::string::npos) {
            auto arr_start = message.find('[', ids_start);
            auto arr_end = message.find(']', arr_start);
            if (arr_start != std::string::npos && arr_end != std::string::npos) {
                std::string ids_str = message.substr(arr_start + 1, arr_end - arr_start - 1);
                std::istringstream ss(ids_str);
                std::string token;
                while (std::getline(ss, token, ',')) {
                    try {
                        request.instrument_ids.push_back(
                            static_cast<uint32_t>(std::stoul(token)));
                    } catch (...) {}
                }
            }
        }
        
        // Parse timestamps
        request.start = core::Timestamp(parse_int64(message, "start"));
        request.end = core::Timestamp(parse_int64(message, "end"));
        request.interval_ms = static_cast<int>(parse_int64(message, "interval_ms"));
        if (request.interval_ms <= 0) request.interval_ms = 1000;
        
        // Parse compare mode
        request.compare_mode = message.find("\"compare_mode\":true") != std::string::npos;
        request.initial_prediction = parse_double(message, "initial_prediction");
        
        if (request.instrument_ids.empty()) {
            send_error("instrument_ids required");
            return;
        }
        
        if (request.start >= request.end) {
            send_error("Invalid time range: start must be before end");
            return;
        }
        
        // Start replay in background thread
        replay_running_ = true;
        replay_thread_ = std::make_unique<std::thread>(
            &ScreenerWSSession::run_replay, this, request);
        
        // Send confirmation
        std::ostringstream json;
        json << "{\"type\":\"replay_started\","
             << "\"instrument_ids\":[";
        for (size_t i = 0; i < request.instrument_ids.size(); ++i) {
            if (i > 0) json << ",";
            json << request.instrument_ids[i];
        }
        json << "],"
             << "\"start\":" << request.start.count() << ","
             << "\"end\":" << request.end.count() << ","
             << "\"interval_ms\":" << request.interval_ms << "}";
        send(json.str());
    }
    
    void run_replay(screener::ReplayRequest request) {
        auto total_duration = (request.end - request.start).count();
        int snapshots_sent = 0;
        auto start_time = std::chrono::steady_clock::now();
        
        screener_->stream_replay(request,
            [this, &snapshots_sent](const screener::ReplaySnapshot& snap) {
                if (!replay_running_) return;  // Cancelled
                
                std::ostringstream json;
                json << "{\"type\":\"replay_tick\","
                     << "\"timestamp\":" << snap.timestamp.count() << ","
                     << "\"instrument_id\":" << snap.instrument_id << ","
                     << "\"tradingsymbol\":\"" << snap.tradingsymbol << "\","
                     << "\"last_price\":" << std::fixed << std::setprecision(2) << snap.last_price << ","
                     << "\"bid_price\":" << snap.bid_price << ","
                     << "\"ask_price\":" << snap.ask_price << ","
                     << "\"volume\":" << snap.volume << ","
                     << "\"oi\":" << snap.oi << ","
                     << "\"price_change\":" << snap.price_change << ","
                     << "\"price_change_pct\":" << snap.price_change_pct;
                
                if (snap.prediction_delta != 0.0) {
                    json << ",\"prediction_delta\":" << snap.prediction_delta;
                }
                
                json << "}";
                send(json.str());
                snapshots_sent++;
            },
            [this, total_duration](double progress) {
                if (!replay_running_) return;
                
                // Send progress every 5%
                static double last_progress = 0;
                if (progress - last_progress >= 5.0) {
                    std::ostringstream json;
                    json << "{\"type\":\"replay_progress\","
                         << "\"progress\":" << std::fixed << std::setprecision(1) << progress << "}";
                    send(json.str());
                    last_progress = progress;
                }
            });
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(end_time - start_time);
        
        // Send completion
        if (replay_running_) {
            std::ostringstream json;
            json << "{\"type\":\"replay_complete\","
                 << "\"snapshots_sent\":" << snapshots_sent << ","
                 << "\"duration_ms\":" << std::fixed << std::setprecision(0) << duration.count() << "}";
            send(json.str());
        }
        
        replay_running_ = false;
    }
    
    void stop_replay() {
        replay_running_ = false;
        
        if (replay_thread_ && replay_thread_->joinable()) {
            replay_thread_->join();
        }
        
        send("{\"type\":\"replay_stopped\"}");
    }
    
    // ========================================================================
    // Market Subscription (for real-time updates)
    // ========================================================================
    
    void handle_subscribe_market(const std::string& message) {
        // Parse timestamp and filter
        auto timestamp = core::Timestamp(parse_int64(message, "timestamp"));
        int refresh_interval = static_cast<int>(parse_int64(message, "refresh_interval_ms"));
        if (refresh_interval <= 0) refresh_interval = 5000;  // Default 5 seconds
        
        // Parse filter from message (simplified)
        screener::ScreenerFilter filter;
        
        auto underlyings_pos = message.find("\"underlyings\"");
        if (underlyings_pos != std::string::npos) {
            // Parse underlyings array
            auto arr_start = message.find('[', underlyings_pos);
            auto arr_end = message.find(']', arr_start);
            if (arr_start != std::string::npos && arr_end != std::string::npos) {
                std::string arr_str = message.substr(arr_start + 1, arr_end - arr_start - 1);
                // Parse comma-separated strings
                size_t pos = 0;
                while (pos < arr_str.length()) {
                    auto q1 = arr_str.find('"', pos);
                    if (q1 == std::string::npos) break;
                    auto q2 = arr_str.find('"', q1 + 1);
                    if (q2 == std::string::npos) break;
                    filter.underlyings.push_back(arr_str.substr(q1 + 1, q2 - q1 - 1));
                    pos = q2 + 1;
                }
            }
        }
        
        filter.limit = 50;  // Limit for streaming
        
        market_subscribed_ = true;
        
        send("{\"type\":\"market_subscribed\"}");
        
        // For actual live market, would integrate with Kite WebSocket here
        // For now, just acknowledge the subscription
    }
    
    void handle_unsubscribe_market() {
        market_subscribed_ = false;
        send("{\"type\":\"market_unsubscribed\"}");
    }
    
    // ========================================================================
    // Helpers
    // ========================================================================
    
    void send(const std::string& message) {
        if (send_callback_) {
            send_callback_(message);
        }
    }
    
    void send_error(const std::string& error) {
        std::ostringstream json;
        json << "{\"type\":\"error\",\"message\":\"" << error << "\"}";
        send(json.str());
    }
    
    int64_t parse_int64(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        auto pos = json.find(search);
        if (pos == std::string::npos) return 0;
        
        pos += search.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        
        auto end = json.find_first_of(",}]", pos);
        if (end == std::string::npos) return 0;
        
        try {
            return std::stoll(json.substr(pos, end - pos));
        } catch (...) {
            return 0;
        }
    }
    
    double parse_double(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        auto pos = json.find(search);
        if (pos == std::string::npos) return 0.0;
        
        pos += search.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        
        auto end = json.find_first_of(",}]", pos);
        if (end == std::string::npos) return 0.0;
        
        try {
            return std::stod(json.substr(pos, end - pos));
        } catch (...) {
            return 0.0;
        }
    }
};

// ============================================================================
// Session Manager
// ============================================================================

class ScreenerWSManager {
public:
    static ScreenerWSManager& instance() {
        static ScreenerWSManager inst;
        return inst;
    }
    
    void initialize() {
        if (!screener_) {
            screener_ = screener::create_screener_service();
        }
    }
    
    void create_session(const std::string& client_id,
                       ScreenerWSSession::SendCallback send_callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        initialize();
        
        sessions_[client_id] = std::make_unique<ScreenerWSSession>(
            client_id, std::move(send_callback), screener_);
    }
    
    void destroy_session(const std::string& client_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(client_id);
    }
    
    void handle_message(const std::string& client_id, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = sessions_.find(client_id);
        if (it != sessions_.end()) {
            it->second->handle_message(message);
        }
    }
    
private:
    ScreenerWSManager() = default;
    
    std::shared_ptr<screener::ScreenerService> screener_;
    std::unordered_map<std::string, std::unique_ptr<ScreenerWSSession>> sessions_;
    std::mutex mutex_;
};

// ============================================================================
// External Interface
// ============================================================================

void screener_ws_on_connect(const std::string& client_id,
                           std::function<void(const std::string&)> send_callback) {
    ScreenerWSManager::instance().create_session(client_id, std::move(send_callback));
}

void screener_ws_on_disconnect(const std::string& client_id) {
    ScreenerWSManager::instance().destroy_session(client_id);
}

void screener_ws_on_message(const std::string& client_id, const std::string& message) {
    ScreenerWSManager::instance().handle_message(client_id, message);
}

} // namespace payoff::api
