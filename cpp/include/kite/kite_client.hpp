#pragma once
/**
 * @file kite_client.hpp
 * @brief Kite Connect REST API client
 * 
 * Implements:
 * - Authentication (login URL, access token)
 * - Profile & margins
 * - Instrument master
 * - LTP/Quote/OHLC
 * 
 * Reference: docs/DeskMetrics/kite_client.py
 */

#include "core/config.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace payoff::kite {

// ============================================================================
// Data Models
// ============================================================================

struct UserProfile {
    std::string user_id;
    std::string user_name;
    std::string email;
    std::string user_type;
    std::string broker;
    std::vector<std::string> exchanges;
    std::vector<std::string> products;
};

struct Margins {
    double equity_available = 0.0;
    double equity_used = 0.0;
    double commodity_available = 0.0;
    double commodity_used = 0.0;
    
    [[nodiscard]] double total_available() const noexcept {
        return equity_available + commodity_available;
    }
};

struct LTPData {
    uint32_t instrument_token = 0;
    double last_price = 0.0;
    int64_t volume = 0;
    std::chrono::system_clock::time_point timestamp;
};

struct QuoteData {
    uint32_t instrument_token = 0;
    double last_price = 0.0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    int64_t volume = 0;
    int64_t buy_quantity = 0;
    int64_t sell_quantity = 0;
    int64_t oi = 0;
    
    // Best bid/ask
    double bid_price = 0.0;
    int64_t bid_quantity = 0;
    double ask_price = 0.0;
    int64_t ask_quantity = 0;
};

struct InstrumentData {
    uint32_t instrument_token = 0;
    uint32_t exchange_token = 0;
    std::string tradingsymbol;
    std::string name;
    std::string exchange;
    std::string segment;
    std::string instrument_type;
    std::optional<double> strike;
    std::optional<std::string> expiry;  // YYYY-MM-DD
    int32_t lot_size = 1;
    double tick_size = 0.05;
};

// ============================================================================
// Error Types
// ============================================================================

enum class KiteError {
    None = 0,
    NetworkError,
    AuthenticationFailed,
    TokenExpired,
    RateLimited,
    InvalidRequest,
    ServerError,
    ParseError
};

struct KiteResult {
    KiteError error = KiteError::None;
    std::string message;
    
    [[nodiscard]] bool ok() const noexcept { return error == KiteError::None; }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
};

// ============================================================================
// Kite Client
// ============================================================================

class KiteClient {
public:
    /**
     * @brief Construct with API key and secret
     */
    explicit KiteClient(const std::string& api_key, const std::string& api_secret);
    
    /**
     * @brief Construct from config
     */
    KiteClient();
    
    ~KiteClient();
    
    // Non-copyable
    KiteClient(const KiteClient&) = delete;
    KiteClient& operator=(const KiteClient&) = delete;
    
    // ========================================================================
    // Authentication
    // ========================================================================
    
    /**
     * @brief Get login URL for browser-based auth
     */
    [[nodiscard]] std::string get_login_url() const;
    
    /**
     * @brief Generate access token from request token
     * @param request_token Token received after login redirect
     * @return Result with access token on success
     */
    KiteResult generate_access_token(const std::string& request_token);
    
    /**
     * @brief Set access token directly (for pre-authenticated sessions)
     */
    void set_access_token(const std::string& token);

    /**
     * @brief Get current access token (empty if not set)
     */
    [[nodiscard]] std::string get_access_token() const;
    
    /**
     * @brief Check if client has valid access token
     */
    [[nodiscard]] bool is_authenticated() const noexcept;
    
    /**
     * @brief Invalidate session (logout)
     */
    KiteResult invalidate_session();
    
    // ========================================================================
    // User Info
    // ========================================================================
    
    /**
     * @brief Get user profile
     */
    [[nodiscard]] std::optional<UserProfile> get_profile();
    
    /**
     * @brief Get margins
     */
    [[nodiscard]] std::optional<Margins> get_margins();
    
    // ========================================================================
    // Instruments
    // ========================================================================
    
    /**
     * @brief Get all instruments for an exchange
     * @param exchange NSE, NFO, BSE, etc.
     * @return Vector of instruments
     */
    [[nodiscard]] std::vector<InstrumentData> get_instruments(const std::string& exchange = "");
    
    /**
     * @brief Download and save instruments to CSV
     * @param exchange Exchange to download
     * @param filepath Output file path
     */
    KiteResult download_instruments(const std::string& exchange, const std::string& filepath);
    
    // ========================================================================
    // Market Data
    // ========================================================================
    
    /**
     * @brief Get LTP for instruments
     * @param instruments List of "exchange:tradingsymbol" strings
     */
    [[nodiscard]] std::unordered_map<std::string, LTPData> get_ltp(
        const std::vector<std::string>& instruments);
    
    /**
     * @brief Get full quote for instruments
     */
    [[nodiscard]] std::unordered_map<std::string, QuoteData> get_quote(
        const std::vector<std::string>& instruments);
    
    /**
     * @brief Get OHLC for instruments
     */
    [[nodiscard]] std::unordered_map<std::string, QuoteData> get_ohlc(
        const std::vector<std::string>& instruments);
    
    // ========================================================================
    // Basket Margins
    // ========================================================================
    
    /**
     * @brief Order for margin calculation
     */
    struct MarginOrder {
        std::string exchange;        // NFO, NSE, etc.
        std::string tradingsymbol;   // e.g., "NIFTY24JAN26300CE"
        std::string transaction_type; // BUY or SELL
        int quantity = 0;
        std::string product;         // NRML, MIS, or CNC
        std::string order_type = "MARKET";
        std::optional<double> price;
        std::optional<double> trigger_price;
        std::string variety = "regular";
    };
    
    /**
     * @brief Margin calculation result
     */
    struct MarginResult {
        double total = 0.0;
        double span = 0.0;
        double exposure = 0.0;
        double option_premium = 0.0;
        double additional = 0.0;
        double var = 0.0;
    };
    
    /**
     * @brief Basket margin response
     */
    struct BasketMarginResponse {
        MarginResult initial;
        MarginResult final_;  // 'final' is reserved keyword
        std::vector<double> per_leg_margins;
        bool success = false;
        std::string error_message;
    };
    
    /**
     * @brief Calculate margin for a basket of orders
     * @param orders List of orders to calculate margin for
     * @param consider_positions Whether to consider existing positions
     * @return Basket margin calculation result
     */
    [[nodiscard]] BasketMarginResponse basket_margins(
        const std::vector<MarginOrder>& orders,
        bool consider_positions = false);
    
    /**
     * @brief Calculate margin for a single order
     */
    [[nodiscard]] std::optional<MarginResult> order_margin(const MarginOrder& order);
    
    // ========================================================================
    // Historical Data
    // ========================================================================
    
    struct OHLCBar {
        std::chrono::system_clock::time_point timestamp;
        double open = 0.0;
        double high = 0.0;
        double low = 0.0;
        double close = 0.0;
        int64_t volume = 0;
        int64_t oi = 0;
    };
    
    /**
     * @brief Get historical OHLC data
     * @param instrument_token Instrument token
     * @param interval minute, day, 3minute, 5minute, etc.
     * @param from Start datetime
     * @param to End datetime
     */
    [[nodiscard]] std::vector<OHLCBar> get_historical_data(
        uint32_t instrument_token,
        const std::string& interval,
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to);
    
    // ========================================================================
    // Getters
    // ========================================================================
    
    [[nodiscard]] const std::string& api_key() const noexcept { return api_key_; }
    [[nodiscard]] const std::string& access_token() const noexcept { return access_token_; }
    [[nodiscard]] KiteError last_error() const noexcept { return last_error_; }
    [[nodiscard]] const std::string& last_error_message() const noexcept { return last_error_message_; }

private:
    std::string api_key_;
    std::string api_secret_;
    std::string access_token_;
    
    KiteError last_error_ = KiteError::None;
    std::string last_error_message_;
    
    // HTTP request helpers
    std::string make_request(const std::string& method, 
                             const std::string& endpoint,
                             const std::unordered_map<std::string, std::string>& params = {});
    
    void set_error(KiteError err, const std::string& msg);
    void clear_error();
    
    // Constants
    static constexpr const char* BASE_URL = "https://api.kite.trade";
    static constexpr const char* LOGIN_URL = "https://kite.zerodha.com/connect/login";
};

// ============================================================================
// Factory
// ============================================================================

/**
 * @brief Create KiteClient from environment/config
 */
std::unique_ptr<KiteClient> create_kite_client();

} // namespace payoff::kite
