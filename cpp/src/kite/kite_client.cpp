/**
 * @file kite_client.cpp
 * @brief Kite Connect REST API client implementation
 * 
 * Uses simple HTTP with sockets - no external dependencies.
 * For production, consider using cpp-httplib or libcurl.
 */

#include "kite/kite_client.hpp"
#include "core/config.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define CLOSE_SOCKET close
#endif

namespace payoff::kite {

// ============================================================================
// SHA256 Implementation (minimal, for checksum)
// ============================================================================

namespace {

// Simple URL encoding
std::string url_encode(const std::string& str) {
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase;
    
    for (char c : str) {
        if (isalnum(static_cast<unsigned char>(c)) || 
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') 
                    << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    
    return encoded.str();
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

#ifdef _WIN32
class WinSockInit {
public:
    WinSockInit() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    ~WinSockInit() {
        WSACleanup();
    }
};
static WinSockInit winsock_init;
#endif

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
}

KiteClient::~KiteClient() = default;

std::string KiteClient::get_login_url() const {
    return std::string(LOGIN_URL) + "?api_key=" + url_encode(api_key_) + "&v=3";
}

void KiteClient::set_access_token(const std::string& token) {
    access_token_ = token;
    clear_error();
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
    
    // This is a simplified HTTP client
    // For production, use cpp-httplib or libcurl
    
    std::string host = "api.kite.trade";
    std::string path = endpoint;
    
    // Build query string for GET or form body for POST
    std::string body;
    if (!params.empty()) {
        for (const auto& [key, value] : params) {
            if (!body.empty()) body += "&";
            body += url_encode(key) + "=" + url_encode(value);
        }
    }
    
    if (method == "GET" && !body.empty()) {
        path += "?" + body;
        body.clear();
    }
    
    // Build HTTP request
    std::ostringstream request;
    request << method << " " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    request << "Connection: close\r\n";
    request << "X-Kite-Version: 3\r\n";
    
    if (!access_token_.empty()) {
        request << "Authorization: token " << api_key_ << ":" << access_token_ << "\r\n";
    }
    
    if (method == "POST" && !body.empty()) {
        request << "Content-Type: application/x-www-form-urlencoded\r\n";
        request << "Content-Length: " << body.length() << "\r\n";
    }
    
    request << "\r\n";
    
    if (!body.empty()) {
        request << body;
    }
    
    // For now, return empty - actual socket code would go here
    // In production, use cpp-httplib which handles SSL properly
    
    // Placeholder response
    set_error(KiteError::NetworkError, "HTTP client not fully implemented - use cpp-httplib");
    return "{}";
}

KiteResult KiteClient::generate_access_token(const std::string& request_token) {
    // Would need SHA256 checksum and proper HTTP
    // checksum = sha256(api_key + request_token + api_secret)
    
    KiteResult result;
    result.error = KiteError::NetworkError;
    result.message = "Token generation requires SHA256 - implement with OpenSSL";
    return result;
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
    
    // This returns CSV data, not JSON
    // Parse CSV and return instruments
    
    std::vector<InstrumentData> instruments;
    
    // CSV parsing would go here
    // Format: instrument_token,exchange_token,tradingsymbol,name,last_price,expiry,strike,tick_size,lot_size,instrument_type,segment,exchange
    
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
// Factory
// ============================================================================

std::unique_ptr<KiteClient> create_kite_client() {
    return std::make_unique<KiteClient>();
}

} // namespace payoff::kite
