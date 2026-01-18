/**
 * @file live_routes.cpp
 * @brief REST API routes for live market data subscription management
 * 
 * Endpoints:
 * - POST /api/live/subscribe - Subscribe to symbols
 * - POST /api/live/unsubscribe - Unsubscribe from symbols
 * - POST /api/live/subscribe/option-chain - Subscribe to option chain
 * - POST /api/live/unsubscribe/option-chain - Unsubscribe from option chain
 * - GET /api/live/subscriptions - Get current subscriptions
 * - GET /api/live/stats - Get subscription statistics
 * - POST /api/live/credentials - Add Kite credentials
 * - DELETE /api/live/credentials/:id - Remove credentials
 * - POST /api/live/start - Start live streaming
 * - POST /api/live/stop - Stop live streaming
 */

#include "kite/subscription_manager.hpp"
#include "core/instrument_manager.hpp"
#include "core/http_client.hpp"
#include "core/config.hpp"
#include "cache/market_cache.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

// Default token service URL (can be overridden in .env as KITE_ACCESS_TOKEN_URL)
static constexpr const char* DEFAULT_TOKEN_SERVICE_URL = "http://110.172.21.62:5005/token/zerodha";

namespace payoff::api {

// ============================================================================
// Global state for live data
// ============================================================================

namespace {

std::shared_ptr<kite::KiteSubscriptionManager> g_subscription_manager;
std::shared_ptr<core::InstrumentManager> g_instrument_manager;
std::shared_ptr<cache::MarketCache> g_market_cache;
std::mutex g_live_mutex;
bool g_live_initialized = false;

// Helper: Parse JSON field from simple JSON response
std::string parse_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    
    // Skip whitespace after colon
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
    if (pos >= json.size() || json[pos] != '"') return "";
    
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    
    return json.substr(pos + 1, end - pos - 1);
}

// Helper: Strip quotes and whitespace from a raw JSON string value
std::string strip_quotes(std::string str) {
    // Trim whitespace first
    while (!str.empty() && (str.back() == '\n' || str.back() == '\r' || str.back() == ' ' || str.back() == '\t')) {
        str.pop_back();
    }
    while (!str.empty() && (str.front() == '\n' || str.front() == '\r' || str.front() == ' ' || str.front() == '\t')) {
        str.erase(0, 1);
    }
    // Then strip quotes
    if (str.size() >= 2 && str.front() == '"' && str.back() == '"') {
        return str.substr(1, str.size() - 2);
    }
    return str;
}

// Fetch credentials from token service (or use static access_token from .env)
bool fetch_credentials_from_token_service(kite::KiteCredentials& creds) {
    auto& cfg = config::config();
    if (!cfg.is_loaded()) {
        cfg.load();
    }
    
    // Get api_key from .env (required)
    std::string api_key = cfg.kite_api_key();
    if (api_key.empty()) {
        std::cerr << "[Live] KITE_API_KEY not set in .env\n";
        return false;
    }
    
    // Check if we already have a static access_token in .env
    std::string access_token = cfg.kite_access_token();
    if (!access_token.empty()) {
        std::cout << "[Live] Using KITE_ACCESS_TOKEN from .env\n";
        creds.api_key = api_key;
        creds.access_token = access_token;
        std::cout << "[Live] Credentials ready: api_key=" << creds.api_key.substr(0, 8) << "...\n";
        return creds.is_valid();
    }
    
    // Otherwise fetch from token service
    std::string token_url = cfg.kite_access_token_url();
    if (token_url.empty()) {
        token_url = DEFAULT_TOKEN_SERVICE_URL;
    }
    
    std::cout << "[Live] Fetching access_token from: " << token_url << "\n";
    
    // Parse URL to get host and path
    std::string host, path;
    bool is_https = false;
    
    if (token_url.substr(0, 8) == "https://") {
        is_https = true;
        token_url = token_url.substr(8);
    } else if (token_url.substr(0, 7) == "http://") {
        token_url = token_url.substr(7);
    }
    
    auto slash_pos = token_url.find('/');
    if (slash_pos != std::string::npos) {
        host = token_url.substr(0, slash_pos);
        path = token_url.substr(slash_pos);
    } else {
        host = token_url;
        path = "/";
    }
    
    core::HttpClient client;
    client.set_base_url((is_https ? "https://" : "http://") + host);
    client.set_timeout(10000);
    
    auto response = client.get(path);
    
    if (!response.ok()) {
        std::cerr << "[Live] Failed to fetch access_token: " 
                  << response.error_message << " (status: " << response.status_code << ")\n";
        return false;
    }
    
    // Token service returns just the access_token as a quoted string: "xxxx"
    access_token = strip_quotes(response.body);
    
    // Trim any whitespace/newlines
    while (!access_token.empty() && (access_token.back() == '\n' || access_token.back() == '\r' || access_token.back() == ' ')) {
        access_token.pop_back();
    }
    
    if (access_token.empty()) {
        std::cerr << "[Live] Token service returned empty access_token\n";
        return false;
    }
    
    creds.api_key = api_key;
    creds.access_token = access_token;
    
    std::cout << "[Live] Credentials ready: api_key=" << creds.api_key.substr(0, 8) << "..., "
              << "access_token=" << creds.access_token.substr(0, 8) << "... (" 
              << creds.access_token.size() << " chars)\n";
    
    return creds.is_valid();
}

} // anonymous namespace

// ============================================================================
// Initialization
// ============================================================================

void init_live_data_service(
    std::shared_ptr<core::InstrumentManager> instrument_manager,
    std::shared_ptr<cache::MarketCache> market_cache) {
    
    std::lock_guard lock(g_live_mutex);
    
    g_instrument_manager = std::move(instrument_manager);
    g_market_cache = std::move(market_cache);
    
    // Create subscription manager
    kite::SubscriptionManagerConfig config;
    config.default_mode = kite::WSMode::Full;  // Need OI + depth
    config.auto_reconnect = true;
    config.auto_redistribute = true;
    
    g_subscription_manager = kite::create_subscription_manager(
        g_instrument_manager, config);
    
    // Set up snapshot callback to update market cache
    g_subscription_manager->on_snapshot([](const core::DepthSnapshot& snap) {
        if (g_market_cache) {
            g_market_cache->update(snap);
        }
    });
    
    g_subscription_manager->on_error([](const std::string& symbol, const std::string& error) {
        std::cerr << "[Live] Error for " << symbol << ": " << error << "\n";
    });
    
    // Try to load credentials from config first
    int cred_idx = g_subscription_manager->add_credentials_from_config();
    
    // If no config credentials, try the token service
    if (cred_idx < 0) {
        kite::KiteCredentials creds;
        if (fetch_credentials_from_token_service(creds)) {
            cred_idx = g_subscription_manager->add_credentials(creds);
        }
    }
    
    // Auto-start if credentials were loaded
    if (cred_idx >= 0) {
        g_subscription_manager->start();
    } else {
        std::cout << "[Live] No credentials available - use POST /api/live/credentials to add\n";
    }
    
    g_live_initialized = true;
    
    std::cout << "[Live] Live data service initialized\n";
}

kite::KiteSubscriptionManager* get_subscription_manager() {
    return g_subscription_manager.get();
}

// ============================================================================
// JSON Helpers
// ============================================================================

namespace {

std::vector<std::string> parse_symbols_from_json(const std::string& json) {
    std::vector<std::string> symbols;
    
    // Find "symbols" array
    size_t pos = json.find("\"symbols\"");
    if (pos == std::string::npos) return symbols;
    
    pos = json.find('[', pos);
    if (pos == std::string::npos) return symbols;
    
    size_t end = json.find(']', pos);
    if (end == std::string::npos) return symbols;
    
    std::string array = json.substr(pos + 1, end - pos - 1);
    
    // Parse each string in array
    size_t start = 0;
    while ((start = array.find('"', start)) != std::string::npos) {
        size_t str_end = array.find('"', start + 1);
        if (str_end == std::string::npos) break;
        
        symbols.push_back(array.substr(start + 1, str_end - start - 1));
        start = str_end + 1;
    }
    
    return symbols;
}

std::string extract_string_field(const std::string& json, const std::string& field) {
    std::string pattern = "\"" + field + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    
    pos = json.find('"', pos + pattern.length());
    if (pos == std::string::npos) return "";
    
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    
    return json.substr(pos + 1, end - pos - 1);
}

std::string build_error_response(const std::string& error) {
    std::ostringstream ss;
    ss << "{\"success\":false,\"error\":\"" << error << "\"}";
    return ss.str();
}

std::string build_success_response(const std::string& message = "") {
    std::ostringstream ss;
    ss << "{\"success\":true";
    if (!message.empty()) {
        ss << ",\"message\":\"" << message << "\"";
    }
    ss << "}";
    return ss.str();
}

} // anonymous namespace

// ============================================================================
// Route Handlers
// ============================================================================

// POST /api/live/subscribe
// Body: {"symbols": ["NFO:49543", "NFO:49544", ...]}
std::string handle_live_subscribe(const std::string& body) {
    if (!g_subscription_manager) {
        return build_error_response("Live data service not initialized");
    }
    
    auto symbols = parse_symbols_from_json(body);
    if (symbols.empty()) {
        return build_error_response("No symbols provided");
    }
    
    size_t subscribed = g_subscription_manager->subscribe_batch(symbols);
    
    std::ostringstream ss;
    ss << "{\"success\":true,\"subscribed\":" << subscribed 
       << ",\"requested\":" << symbols.size() << "}";
    return ss.str();
}

// POST /api/live/unsubscribe
// Body: {"symbols": ["NFO:49543", "NFO:49544", ...]}
std::string handle_live_unsubscribe(const std::string& body) {
    if (!g_subscription_manager) {
        return build_error_response("Live data service not initialized");
    }
    
    auto symbols = parse_symbols_from_json(body);
    if (symbols.empty()) {
        return build_error_response("No symbols provided");
    }
    
    g_subscription_manager->unsubscribe_batch(symbols);
    
    std::ostringstream ss;
    ss << "{\"success\":true,\"unsubscribed\":" << symbols.size() << "}";
    return ss.str();
}

// POST /api/live/subscribe/option-chain
// Body: {"underlying": "NIFTY", "expiry": "2025-01-30"}  // expiry optional
std::string handle_live_subscribe_option_chain(const std::string& body) {
    if (!g_subscription_manager) {
        return build_error_response("Live data service not initialized");
    }
    
    std::string underlying = extract_string_field(body, "underlying");
    if (underlying.empty()) {
        return build_error_response("No underlying provided");
    }
    
    std::string expiry_str = extract_string_field(body, "expiry");
    std::optional<std::chrono::year_month_day> expiry;
    
    if (!expiry_str.empty()) {
        // Parse YYYY-MM-DD
        int year, month, day;
        if (std::sscanf(expiry_str.c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
            expiry = std::chrono::year_month_day{
                std::chrono::year{year},
                std::chrono::month{static_cast<unsigned>(month)},
                std::chrono::day{static_cast<unsigned>(day)}
            };
        }
    }
    
    size_t count = g_subscription_manager->subscribe_option_chain(underlying, expiry);
    
    std::ostringstream ss;
    ss << "{\"success\":true,\"underlying\":\"" << underlying 
       << "\",\"subscribed\":" << count << "}";
    return ss.str();
}

// POST /api/live/unsubscribe/option-chain
// Body: {"underlying": "NIFTY", "expiry": "2025-01-30"}  // expiry optional
std::string handle_live_unsubscribe_option_chain(const std::string& body) {
    if (!g_subscription_manager) {
        return build_error_response("Live data service not initialized");
    }
    
    std::string underlying = extract_string_field(body, "underlying");
    if (underlying.empty()) {
        return build_error_response("No underlying provided");
    }
    
    std::string expiry_str = extract_string_field(body, "expiry");
    std::optional<std::chrono::year_month_day> expiry;
    
    if (!expiry_str.empty()) {
        int year, month, day;
        if (std::sscanf(expiry_str.c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
            expiry = std::chrono::year_month_day{
                std::chrono::year{year},
                std::chrono::month{static_cast<unsigned>(month)},
                std::chrono::day{static_cast<unsigned>(day)}
            };
        }
    }
    
    g_subscription_manager->unsubscribe_option_chain(underlying, expiry);
    
    return build_success_response("Option chain unsubscribed");
}

// GET /api/live/subscriptions
std::string handle_live_get_subscriptions() {
    if (!g_subscription_manager) {
        return build_error_response("Live data service not initialized");
    }
    
    auto symbols = g_subscription_manager->get_subscribed_symbols();
    
    std::ostringstream ss;
    ss << "{\"success\":true,\"count\":" << symbols.size() << ",\"symbols\":[";
    
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << symbols[i] << "\"";
    }
    
    ss << "]}";
    return ss.str();
}

// GET /api/live/stats
std::string handle_live_get_stats() {
    if (!g_subscription_manager) {
        return build_error_response("Live data service not initialized");
    }
    
    auto stats = g_subscription_manager->get_stats();
    
    std::ostringstream ss;
    ss << "{\"success\":true,"
       << "\"credentials\":" << stats.total_credentials << ","
       << "\"active_connections\":" << stats.active_connections << ","
       << "\"subscribed\":" << stats.total_subscribed << ","
       << "\"capacity\":" << stats.total_capacity << ","
       << "\"ticks_received\":" << stats.ticks_received << ","
       << "\"is_running\":" << (g_subscription_manager->is_running() ? "true" : "false")
       << "}";
    return ss.str();
}

// POST /api/live/credentials
// Body: {"api_key": "xxx", "access_token": "xxx", "user_id": "xxx"}
std::string handle_live_add_credentials(const std::string& body) {
    if (!g_subscription_manager) {
        return build_error_response("Live data service not initialized");
    }
    
    kite::KiteCredentials creds;
    creds.api_key = extract_string_field(body, "api_key");
    creds.access_token = extract_string_field(body, "access_token");
    creds.user_id = extract_string_field(body, "user_id");
    
    if (creds.api_key.empty() || creds.access_token.empty()) {
        return build_error_response("api_key and access_token required");
    }
    
    int index = g_subscription_manager->add_credentials(creds);
    
    if (index < 0) {
        return build_error_response("Failed to add credentials");
    }
    
    std::ostringstream ss;
    ss << "{\"success\":true,\"index\":" << index << "}";
    return ss.str();
}

// DELETE /api/live/credentials/:id
std::string handle_live_remove_credentials(size_t index) {
    if (!g_subscription_manager) {
        return build_error_response("Live data service not initialized");
    }
    
    g_subscription_manager->remove_credentials(index);
    
    return build_success_response("Credentials removed");
}

// POST /api/live/start
std::string handle_live_start() {
    if (!g_subscription_manager) {
        return build_error_response("Live data service not initialized");
    }
    
    if (g_subscription_manager->credential_count() == 0) {
        // Try to add credentials from config
        int idx = g_subscription_manager->add_credentials_from_config();
        if (idx < 0) {
            return build_error_response("No credentials available. Add credentials first.");
        }
    }
    
    g_subscription_manager->start();
    
    return build_success_response("Live streaming started");
}

// POST /api/live/stop
std::string handle_live_stop() {
    if (!g_subscription_manager) {
        return build_error_response("Live data service not initialized");
    }
    
    g_subscription_manager->stop();
    
    return build_success_response("Live streaming stopped");
}

// POST /api/live/replace
// Body: {"symbols": ["NFO:49543", ...]}
// Efficiently replaces current subscriptions with new set
std::string handle_live_replace_subscriptions(const std::string& body) {
    if (!g_subscription_manager) {
        return build_error_response("Live data service not initialized");
    }
    
    auto symbols = parse_symbols_from_json(body);
    
    g_subscription_manager->replace_subscriptions(symbols);
    
    std::ostringstream ss;
    ss << "{\"success\":true,\"new_count\":" << symbols.size() << "}";
    return ss.str();
}

// ============================================================================
// Route Setup Helper
// ============================================================================

/**
 * @brief Register live data routes with REST server
 * 
 * Call this from rest_server.cpp setup function.
 */
void setup_live_routes();  // Forward declaration - implemented in rest_server.cpp

} // namespace payoff::api
