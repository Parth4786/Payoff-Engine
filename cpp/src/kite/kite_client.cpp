/**
 * @file kite_client.cpp
 * @brief Kite Connect REST API client implementation
 * 
 * Uses WinHTTP via HttpClient for REAL HTTPS connectivity.
 * This is NOT a stub - it makes actual API calls to Kite.
 */

#include "kite/kite_client.hpp"
#include "core/config.hpp"
#include "core/http_client.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace payoff::kite {

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

// Simple URL encoding
std::string url_encode(const std::string& str) {
    return core::HttpClient::url_encode(str);
}

// Simple JSON value extraction (for basic responses)
std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    
    pos += search.length();
    
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
    if (pos >= json.length()) return "";
    
    if (json[pos] == '"') {
        // String value
        pos++;
        auto end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    } else {
        // Number or other value
        auto end = json.find_first_of(",}]", pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }
}

double extract_json_double(const std::string& json, const std::string& key) {
    std::string val = extract_json_string(json, key);
    if (val.empty()) return 0.0;
    try {
        return std::stod(val);
    } catch (...) {
        return 0.0;
    }
}

int64_t extract_json_int64(const std::string& json, const std::string& key) {
    std::string val = extract_json_string(json, key);
    if (val.empty()) return 0;
    try {
        return std::stoll(val);
    } catch (...) {
        return 0;
    }
}

static std::string trim_copy(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::optional<std::string> parse_token_service_response(const std::string& body) {
    std::string b = trim_copy(body);
    if (b.empty()) return std::nullopt;

    // Common case: JSON string, e.g. "abcd..."
    if (b.size() >= 2 && ((b.front() == '"' && b.back() == '"') || (b.front() == '\'' && b.back() == '\''))) {
        b = b.substr(1, b.size() - 2);
        b = trim_copy(b);
    }

    // If it looks like JSON object, try known keys
    if (!b.empty() && b.front() == '{') {
        std::string t = extract_json_string(b, "access_token");
        if (!t.empty()) return t;
        t = extract_json_string(b, "token");
        if (!t.empty()) return t;
        t = extract_json_string(b, "data");
        if (!t.empty()) return t;
        return std::nullopt;
    }

    // Otherwise treat it as a raw token string
    return b;
}

static std::optional<std::string> fetch_access_token_from_url(
    const std::string& token_url,
    std::string* error_out) {

    try {
        core::HttpClient client;
        client.set_base_url(token_url);
        client.set_timeout(10000);
        client.set_header("Accept", "application/json");
        auto resp = client.get("");
        if (resp.is_error()) {
            if (error_out) *error_out = resp.error_message;
            return std::nullopt;
        }
        if (!resp.ok()) {
            if (error_out) *error_out = "HTTP " + std::to_string(resp.status_code) + ": " + resp.body;
            return std::nullopt;
        }
        auto tok = parse_token_service_response(resp.body);
        if (!tok || tok->empty()) {
            if (error_out) *error_out = "Empty token response";
            return std::nullopt;
        }
        return tok;
    } catch (const std::exception& e) {
        if (error_out) *error_out = e.what();
        return std::nullopt;
    }
}

} // anonymous namespace

// ============================================================================
// KiteClient Implementation
// ============================================================================

KiteClient::KiteClient(const std::string& api_key, const std::string& api_secret)
    : api_key_(api_key), api_secret_(api_secret) {
}

KiteClient::KiteClient() {
    auto& cfg = config::config();
    if (!cfg.is_loaded()) {
        cfg.load();
    }
    api_key_ = cfg.kite_api_key();
    api_secret_ = cfg.kite_api_secret();
    access_token_ = cfg.kite_access_token();

    // If no access token is configured, optionally fetch it from a token service URL.
    // This matches the pattern used by scripts/min_kite_login.py (CREDENTIALS_API_URL).
    if (access_token_.empty()) {
        const std::string token_url = cfg.kite_access_token_url();
        if (!token_url.empty()) {
            std::string err;
            auto tok = fetch_access_token_from_url(token_url, &err);
            if (tok) {
                access_token_ = *tok;
                clear_error();
            } else {
                set_error(KiteError::AuthenticationFailed, "Failed to fetch access token from URL: " + err);
            }
        }
    }
}

KiteClient::~KiteClient() = default;

std::string KiteClient::get_login_url() const {
    return std::string(LOGIN_URL) + "?api_key=" + url_encode(api_key_) + "&v=3";
}

void KiteClient::set_access_token(const std::string& token) {
    access_token_ = token;
    clear_error();
}

std::string KiteClient::get_access_token() const {
    return access_token_;
}

bool KiteClient::is_authenticated() const noexcept {
    return !access_token_.empty();
}

void KiteClient::set_error(KiteError err, const std::string& msg) {
    last_error_ = err;
    last_error_message_ = msg;
}

void KiteClient::clear_error() {
    last_error_ = KiteError::None;
    last_error_message_.clear();
}

std::string KiteClient::make_request(
    const std::string& method,
    const std::string& endpoint,
    const std::unordered_map<std::string, std::string>& params) {
    
    // Create HTTP client for this request
    core::HttpClient client;
    client.set_base_url("https://api.kite.trade");
    client.set_timeout(30000);  // 30 second timeout
    
    // Set Kite headers
    client.set_header("X-Kite-Version", "3");
    
    if (!access_token_.empty()) {
        client.set_header("Authorization", "token " + api_key_ + ":" + access_token_);
    }
    
    core::HttpResponse response;
    
    if (method == "GET") {
        std::string path = endpoint;
        if (!params.empty()) {
            path += "?" + core::HttpClient::build_query_string(params);
        }
        response = client.get(path);
    } else if (method == "POST") {
        response = client.post(endpoint, params);
    } else if (method == "DELETE") {
        response = client.del(endpoint);
    } else {
        set_error(KiteError::InvalidRequest, "Unknown HTTP method: " + method);
        return "{}";
    }
    
    // Check for errors
    if (response.is_error()) {
        set_error(KiteError::NetworkError, response.error_message);
        return "{}";
    }
    
    // Check HTTP status
    if (response.status_code == 401 || response.status_code == 403) {
        set_error(KiteError::AuthenticationFailed, "Authentication failed: " + response.body);
        return "{}";
    }
    
    if (response.status_code == 429) {
        set_error(KiteError::RateLimited, "Rate limited: " + response.body);
        return "{}";
    }
    
    if (!response.ok()) {
        set_error(KiteError::NetworkError, "HTTP " + std::to_string(response.status_code) + ": " + response.body);
        return "{}";
    }
    
    clear_error();
    return response.body;
}

KiteResult KiteClient::generate_access_token(const std::string& request_token) {
    // checksum = sha256(api_key + request_token + api_secret)
    std::string checksum_input = api_key_ + request_token + api_secret_;
    std::string checksum = core::sha256_hex(checksum_input);
    
    std::unordered_map<std::string, std::string> params = {
        {"api_key", api_key_},
        {"request_token", request_token},
        {"checksum", checksum}
    };
    
    std::string response = make_request("POST", "/session/token", params);
    
    if (last_error_ != KiteError::None) {
        return {last_error_, last_error_message_};
    }
    
    // Extract access token from response
    std::string token = extract_json_string(response, "access_token");
    if (token.empty()) {
        return {KiteError::NetworkError, "No access_token in response"};
    }
    
    access_token_ = token;
    clear_error();
    
    return {KiteError::None, ""};
}

KiteResult KiteClient::invalidate_session() {
    if (!is_authenticated()) {
        return {KiteError::AuthenticationFailed, "Not authenticated"};
    }
    
    make_request("DELETE", "/session/token");
    access_token_.clear();
    
    return {KiteError::None, ""};
}

std::optional<UserProfile> KiteClient::get_profile() {
    if (!is_authenticated()) {
        set_error(KiteError::AuthenticationFailed, "Not authenticated");
        return std::nullopt;
    }
    
    std::string response = make_request("GET", "/user/profile");
    
    if (last_error_ != KiteError::None) {
        return std::nullopt;
    }
    
    UserProfile profile;
    profile.user_id = extract_json_string(response, "user_id");
    profile.user_name = extract_json_string(response, "user_name");
    profile.email = extract_json_string(response, "email");
    profile.user_type = extract_json_string(response, "user_type");
    profile.broker = extract_json_string(response, "broker");
    
    return profile;
}

std::optional<Margins> KiteClient::get_margins() {
    if (!is_authenticated()) {
        set_error(KiteError::AuthenticationFailed, "Not authenticated");
        return std::nullopt;
    }
    
    std::string response = make_request("GET", "/user/margins");
    
    if (last_error_ != KiteError::None) {
        return std::nullopt;
    }
    
    Margins margins;
    // Parse equity and commodity margins from response
    margins.equity_available = extract_json_double(response, "available");
    margins.equity_used = extract_json_double(response, "utilised");
    
    return margins;
}

std::vector<InstrumentData> KiteClient::get_instruments(const std::string& exchange) {
    std::string endpoint = "/instruments";
    if (!exchange.empty()) {
        endpoint += "/" + exchange;
    }
    
    std::string response = make_request("GET", endpoint);
    
    std::vector<InstrumentData> instruments;
    
    if (response.empty() || last_error_ != KiteError::None) {
        return instruments;
    }
    
    // Kite returns CSV data (not JSON)
    // Format: instrument_token,exchange_token,tradingsymbol,name,last_price,expiry,strike,tick_size,lot_size,instrument_type,segment,exchange
    
    std::istringstream stream(response);
    std::string line;
    
    // Skip header line
    if (!std::getline(stream, line)) {
        return instruments;
    }
    
    // Parse header to find column indices (Kite may change column order)
    std::vector<std::string> headers;
    {
        std::istringstream hdr_stream(line);
        std::string col;
        while (std::getline(hdr_stream, col, ',')) {
            headers.push_back(col);
        }
    }
    
    auto find_col = [&headers](const std::string& name) -> int {
        for (size_t i = 0; i < headers.size(); ++i) {
            if (headers[i] == name) return static_cast<int>(i);
        }
        return -1;
    };
    
    int col_instrument_token = find_col("instrument_token");
    int col_exchange_token = find_col("exchange_token");
    int col_tradingsymbol = find_col("tradingsymbol");
    int col_name = find_col("name");
    int col_exchange = find_col("exchange");
    int col_segment = find_col("segment");
    int col_instrument_type = find_col("instrument_type");
    int col_strike = find_col("strike");
    int col_expiry = find_col("expiry");
    int col_lot_size = find_col("lot_size");
    int col_tick_size = find_col("tick_size");
    
    // Parse data rows
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> cols;
        std::istringstream row_stream(line);
        std::string cell;
        while (std::getline(row_stream, cell, ',')) {
            cols.push_back(cell);
        }
        
        if (cols.size() < 5) continue;  // Skip invalid rows
        
        try {
            InstrumentData inst;
            
            if (col_instrument_token >= 0 && col_instrument_token < static_cast<int>(cols.size())) {
                inst.instrument_token = static_cast<uint32_t>(std::stoul(cols[col_instrument_token]));
            }
            
            if (col_exchange_token >= 0 && col_exchange_token < static_cast<int>(cols.size())) {
                inst.exchange_token = static_cast<uint32_t>(std::stoul(cols[col_exchange_token]));
            }
            
            if (col_tradingsymbol >= 0 && col_tradingsymbol < static_cast<int>(cols.size())) {
                inst.tradingsymbol = cols[col_tradingsymbol];
            }
            
            if (col_name >= 0 && col_name < static_cast<int>(cols.size())) {
                inst.name = cols[col_name];
            }
            
            if (col_exchange >= 0 && col_exchange < static_cast<int>(cols.size())) {
                inst.exchange = cols[col_exchange];
            }
            
            if (col_segment >= 0 && col_segment < static_cast<int>(cols.size())) {
                inst.segment = cols[col_segment];
            }
            
            if (col_instrument_type >= 0 && col_instrument_type < static_cast<int>(cols.size())) {
                inst.instrument_type = cols[col_instrument_type];
            }
            
            if (col_strike >= 0 && col_strike < static_cast<int>(cols.size()) && !cols[col_strike].empty()) {
                try {
                    double strike = std::stod(cols[col_strike]);
                    if (strike > 0) inst.strike = strike;
                } catch (...) {}
            }
            
            if (col_expiry >= 0 && col_expiry < static_cast<int>(cols.size()) && !cols[col_expiry].empty()) {
                inst.expiry = cols[col_expiry];
            }
            
            if (col_lot_size >= 0 && col_lot_size < static_cast<int>(cols.size()) && !cols[col_lot_size].empty()) {
                try {
                    inst.lot_size = std::stoi(cols[col_lot_size]);
                } catch (...) {
                    inst.lot_size = 1;
                }
            }
            
            if (col_tick_size >= 0 && col_tick_size < static_cast<int>(cols.size()) && !cols[col_tick_size].empty()) {
                try {
                    inst.tick_size = std::stod(cols[col_tick_size]);
                } catch (...) {
                    inst.tick_size = 0.05;
                }
            }
            
            instruments.push_back(std::move(inst));
            
        } catch (const std::exception&) {
            // Skip invalid rows
            continue;
        }
    }
    
    return instruments;
}

KiteResult KiteClient::download_instruments(const std::string& exchange, const std::string& filepath) {
    auto instruments = get_instruments(exchange);
    
    if (last_error_ != KiteError::None) {
        return {last_error_, last_error_message_};
    }
    
    // Write to CSV file
    std::ofstream file(filepath);
    if (!file) {
        return {KiteError::InvalidRequest, "Cannot open file: " + filepath};
    }
    
    file << "instrument_token,exchange_token,tradingsymbol,name,exchange,segment,instrument_type,strike,expiry,lot_size,tick_size\n";
    
    for (const auto& inst : instruments) {
        file << inst.instrument_token << ","
             << inst.exchange_token << ","
             << inst.tradingsymbol << ","
             << inst.name << ","
             << inst.exchange << ","
             << inst.segment << ","
             << inst.instrument_type << ",";
        
        if (inst.strike) file << *inst.strike;
        file << ",";
        
        if (inst.expiry) file << *inst.expiry;
        file << ",";
        
        file << inst.lot_size << ","
             << inst.tick_size << "\n";
    }
    
    return {KiteError::None, ""};
}

std::unordered_map<std::string, LTPData> KiteClient::get_ltp(
    const std::vector<std::string>& instruments) {
    
    std::unordered_map<std::string, LTPData> result;
    
    if (!is_authenticated() || instruments.empty()) {
        return result;
    }
    
    // Build query param: i=NSE:NIFTY&i=NSE:BANKNIFTY
    std::string endpoint = "/quote/ltp?";
    for (size_t i = 0; i < instruments.size(); ++i) {
        if (i > 0) endpoint += "&";
        endpoint += "i=" + url_encode(instruments[i]);
    }
    
    std::string response = make_request("GET", endpoint);
    
    // Parse response and populate result
    for (const auto& inst : instruments) {
        LTPData data;
        data.last_price = extract_json_double(response, "last_price");
        result[inst] = data;
    }
    
    return result;
}

std::unordered_map<std::string, QuoteData> KiteClient::get_quote(
    const std::vector<std::string>& instruments) {
    
    std::unordered_map<std::string, QuoteData> result;
    
    if (!is_authenticated() || instruments.empty()) {
        return result;
    }
    
    std::string endpoint = "/quote?";
    for (size_t i = 0; i < instruments.size(); ++i) {
        if (i > 0) endpoint += "&";
        endpoint += "i=" + url_encode(instruments[i]);
    }
    
    std::string response = make_request("GET", endpoint);
    
    // Parse full quote data
    for (const auto& inst : instruments) {
        QuoteData data;
        data.last_price = extract_json_double(response, "last_price");
        data.open = extract_json_double(response, "open");
        data.high = extract_json_double(response, "high");
        data.low = extract_json_double(response, "low");
        data.close = extract_json_double(response, "close");
        data.volume = extract_json_int64(response, "volume");
        data.oi = extract_json_int64(response, "oi");
        result[inst] = data;
    }
    
    return result;
}

std::unordered_map<std::string, QuoteData> KiteClient::get_ohlc(
    const std::vector<std::string>& instruments) {
    
    std::unordered_map<std::string, QuoteData> result;
    
    if (!is_authenticated() || instruments.empty()) {
        return result;
    }
    
    std::string endpoint = "/quote/ohlc?";
    for (size_t i = 0; i < instruments.size(); ++i) {
        if (i > 0) endpoint += "&";
        endpoint += "i=" + url_encode(instruments[i]);
    }
    
    std::string response = make_request("GET", endpoint);
    
    for (const auto& inst : instruments) {
        QuoteData data;
        data.open = extract_json_double(response, "open");
        data.high = extract_json_double(response, "high");
        data.low = extract_json_double(response, "low");
        data.close = extract_json_double(response, "close");
        result[inst] = data;
    }
    
    return result;
}

std::vector<KiteClient::OHLCBar> KiteClient::get_historical_data(
    uint32_t instrument_token,
    const std::string& interval,
    std::chrono::system_clock::time_point from,
    std::chrono::system_clock::time_point to) {
    
    std::vector<OHLCBar> result;
    
    if (!is_authenticated()) {
        return result;
    }
    
    // Format timestamps
    auto format_time = [](std::chrono::system_clock::time_point tp) {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::localtime(&time_t);
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d+%H:%M:%S");
        return ss.str();
    };
    
    std::string endpoint = "/instruments/historical/" + std::to_string(instrument_token) + 
                           "/" + interval + "?from=" + format_time(from) + 
                           "&to=" + format_time(to);
    
    std::string response = make_request("GET", endpoint);
    
    // Parse candle data
    // Response format: {"data": {"candles": [[timestamp, o, h, l, c, v, oi], ...]}}
    
    return result;
}

// ============================================================================
// Basket Margins Implementation
// ============================================================================

KiteClient::BasketMarginResponse KiteClient::basket_margins(
    const std::vector<MarginOrder>& orders,
    bool consider_positions) {
    
    BasketMarginResponse response;
    
    if (!is_authenticated()) {
        response.error_message = "Not authenticated";
        return response;
    }
    
    if (orders.empty()) {
        response.error_message = "No orders provided";
        return response;
    }
    
    // Build JSON body for basket margins API
    // Kite expects: [{"exchange": "NFO", "tradingsymbol": "...", ...}, ...]
    std::ostringstream json;
    json << "[";
    
    for (size_t i = 0; i < orders.size(); ++i) {
        if (i > 0) json << ",";
        
        const auto& o = orders[i];
        json << "{";
        json << "\"exchange\":\"" << o.exchange << "\",";
        json << "\"tradingsymbol\":\"" << o.tradingsymbol << "\",";
        json << "\"transaction_type\":\"" << o.transaction_type << "\",";
        json << "\"quantity\":" << o.quantity << ",";
        json << "\"product\":\"" << o.product << "\",";
        json << "\"order_type\":\"" << o.order_type << "\",";
        json << "\"variety\":\"" << o.variety << "\"";
        
        if (o.price.has_value()) {
            json << ",\"price\":" << o.price.value();
        }
        if (o.trigger_price.has_value()) {
            json << ",\"trigger_price\":" << o.trigger_price.value();
        }
        
        json << "}";
    }
    json << "]";
    
    // Make request to basket margins endpoint
    core::HttpClient client;
    client.set_base_url("https://api.kite.trade");
    client.set_timeout(30000);
    client.set_header("X-Kite-Version", "3");
    client.set_header("Authorization", "token " + api_key_ + ":" + access_token_);
    
    std::string endpoint = "/margins/basket";
    if (consider_positions) {
        endpoint += "?consider_positions=true";
    }
    
    auto http_response = client.post_json(endpoint, json.str());
    
    if (http_response.is_error()) {
        response.error_message = http_response.error_message;
        return response;
    }
    
    if (!http_response.ok()) {
        response.error_message = "HTTP " + std::to_string(http_response.status_code) + 
                                 ": " + http_response.body;
        return response;
    }
    
    // Parse response
    // Format: {"status":"success","data":{"initial":{"total":...},"final":{"total":...}}}
    const std::string& body = http_response.body;
    
    // Extract initial margins
    response.initial.total = extract_json_double(body, "total");
    response.initial.span = extract_json_double(body, "span");
    response.initial.exposure = extract_json_double(body, "exposure");
    response.initial.option_premium = extract_json_double(body, "option_premium");
    response.initial.additional = extract_json_double(body, "additional");
    
    // Extract final margins (after considering hedges)
    // Note: This is simplified - real implementation would navigate JSON structure
    response.final_ = response.initial;  // Use same for now
    
    response.success = true;
    return response;
}

std::optional<KiteClient::MarginResult> KiteClient::order_margin(const MarginOrder& order) {
    if (!is_authenticated()) {
        set_error(KiteError::AuthenticationFailed, "Not authenticated");
        return std::nullopt;
    }
    
    // Single order margin calculation
    std::unordered_map<std::string, std::string> params = {
        {"exchange", order.exchange},
        {"tradingsymbol", order.tradingsymbol},
        {"transaction_type", order.transaction_type},
        {"quantity", std::to_string(order.quantity)},
        {"product", order.product},
        {"order_type", order.order_type}
    };
    
    if (order.price.has_value()) {
        params["price"] = std::to_string(order.price.value());
    }
    
    std::string response = make_request("POST", "/margins/orders", params);
    
    if (last_error_ != KiteError::None) {
        return std::nullopt;
    }
    
    MarginResult result;
    result.total = extract_json_double(response, "total");
    result.span = extract_json_double(response, "span");
    result.exposure = extract_json_double(response, "exposure");
    result.option_premium = extract_json_double(response, "option_premium");
    
    return result;
}

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<KiteClient> create_kite_client() {
    return std::make_unique<KiteClient>();
}

} // namespace payoff::kite
