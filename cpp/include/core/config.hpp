#pragma once
/**
 * @file config.hpp
 * @brief Configuration management - loads settings from .env file
 * 
 * Provides centralized access to:
 * - ClickHouse connection settings
 * - Kite API credentials
 * - Feature engine parameters
 * - Streaming configuration
 */

#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

namespace payoff::config {

/**
 * @brief Configuration manager - singleton pattern
 */
class Config {
public:
    static Config& instance() {
        static Config config;
        return config;
    }
    
    /**
     * @brief Load configuration from .env file
     * @param filepath Path to .env file
     * @return true if loaded successfully
     */
    bool load(const std::string& filepath = ".env") {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            // Try parent directory
            file.open("../.env");
            if (!file.is_open()) {
                return false;
            }
        }
        
        std::string line;
        while (std::getline(file, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;
            
            // Find the = separator
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            
            std::string key = trim(line.substr(0, pos));
            std::string value = trim(line.substr(pos + 1));
            
            // Remove quotes if present
            if (value.size() >= 2) {
                if ((value.front() == '"' && value.back() == '"') ||
                    (value.front() == '\'' && value.back() == '\'')) {
                    value = value.substr(1, value.size() - 2);
                }
            }
            
            values_[key] = value;
        }
        
        loaded_ = true;
        return true;
    }
    
    /**
     * @brief Get string value
     */
    [[nodiscard]] std::string get(const std::string& key, 
                                   const std::string& default_value = "") const {
        auto it = values_.find(key);
        if (it != values_.end()) {
            return it->second;
        }
        // Also check environment variable
        const char* env = std::getenv(key.c_str());
        if (env) {
            return std::string(env);
        }
        return default_value;
    }
    
    /**
     * @brief Get integer value
     */
    [[nodiscard]] int get_int(const std::string& key, int default_value = 0) const {
        std::string val = get(key);
        if (val.empty()) return default_value;
        try {
            return std::stoi(val);
        } catch (...) {
            return default_value;
        }
    }
    
    /**
     * @brief Get double value
     */
    [[nodiscard]] double get_double(const std::string& key, double default_value = 0.0) const {
        std::string val = get(key);
        if (val.empty()) return default_value;
        try {
            return std::stod(val);
        } catch (...) {
            return default_value;
        }
    }
    
    /**
     * @brief Get boolean value
     */
    [[nodiscard]] bool get_bool(const std::string& key, bool default_value = false) const {
        std::string val = get(key);
        if (val.empty()) return default_value;
        return val == "true" || val == "True" || val == "1" || val == "yes";
    }
    
    [[nodiscard]] bool is_loaded() const noexcept { return loaded_; }
    
    // ========================================================================
    // ClickHouse Configuration
    // ========================================================================
    [[nodiscard]] std::string ch_host() const { return get("CH_HOST", "localhost"); }
    [[nodiscard]] int ch_port() const { return get_int("CH_PORT", 8123); }
    [[nodiscard]] std::string ch_username() const { return get("CH_USERNAME", "default"); }
    [[nodiscard]] std::string ch_password() const { return get("CH_PASSWORD", ""); }
    [[nodiscard]] std::string ch_database() const { return get("CH_DATABASE", "tick_data_db"); }
    [[nodiscard]] std::string ch_table() const { return get("CH_TABLE", "market_data"); }
    [[nodiscard]] int ch_poll_interval_ms() const { return get_int("CH_POLL_INTERVAL_MS", 50); }
    [[nodiscard]] int ch_batch_size() const { return get_int("CH_BATCH_SIZE", 500); }
    
    // ========================================================================
    // Kite Configuration
    // ========================================================================
    [[nodiscard]] std::string kite_api_key() const { return get("KITE_API_KEY", ""); }
    [[nodiscard]] std::string kite_api_secret() const { return get("KITE_API_SECRET", ""); }
    [[nodiscard]] std::string kite_user_id() const { return get("KITE_USER_ID", ""); }
    [[nodiscard]] std::string kite_password() const { return get("KITE_PASSWORD", ""); }
    [[nodiscard]] std::string kite_totp_secret() const { return get("KITE_TOTP_SECRET", ""); }
    [[nodiscard]] std::string kite_access_token() const { return get("KITE_ACCESS_TOKEN", ""); }
    [[nodiscard]] std::string kite_instrument_dir() const { 
        return get("KITE_INSTRUMENT_MASTER_DIR", ""); 
    }
    [[nodiscard]] std::string kite_ws_mode() const { return get("KITE_WS_MODE", "full"); }
    
    // ========================================================================
    // Feature Engine Configuration
    // ========================================================================
    [[nodiscard]] double feature_price_eps() const { 
        return get_double("FEATURE_PRICE_EPS", 0.0001); 
    }
    [[nodiscard]] double feature_depth_slope_eps() const { 
        return get_double("FEATURE_DEPTH_SLOPE_EPS", 0.0001); 
    }
    [[nodiscard]] double feature_spread_floor() const { 
        return get_double("FEATURE_SPREAD_FLOOR", 0.0001); 
    }
    [[nodiscard]] double feature_liquidity_shock_threshold() const { 
        return get_double("FEATURE_LIQUIDITY_SHOCK_THRESHOLD", 0.6); 
    }
    
    // ========================================================================
    // Streaming Configuration
    // ========================================================================
    [[nodiscard]] int stream_gap_threshold_ms() const { 
        return get_int("STREAM_GAP_THRESHOLD_MS", 5000); 
    }
    [[nodiscard]] int stream_ui_emit_interval_ms() const { 
        return get_int("STREAM_UI_EMIT_INTERVAL_MS", 100); 
    }
    
    // ========================================================================
    // Execution Hint Configuration
    // ========================================================================
    [[nodiscard]] int exec_rolling_window() const { 
        return get_int("EXEC_ROLLING_WINDOW", 100); 
    }
    [[nodiscard]] int exec_min_history() const { 
        return get_int("EXEC_MIN_HISTORY", 20); 
    }
    [[nodiscard]] double exec_depth_strong_pct() const { 
        return get_double("EXEC_DEPTH_STRONG_PCT", 0.6); 
    }
    
    // ========================================================================
    // Risk/Kill Switch Configuration
    // ========================================================================
    [[nodiscard]] bool kill_partial_is_fatal() const { 
        return get_bool("KILL_PARTIAL_IS_FATAL", true); 
    }
    [[nodiscard]] double risk_min_tradeable_score() const { 
        return get_double("RISK_MIN_TRADEABLE_SCORE", 0.5); 
    }
    
    // ========================================================================
    // Live Mode
    // ========================================================================
    [[nodiscard]] std::string live_mode() const { 
        return get("LIVE_MODE", "kite_only"); 
    }
    
private:
    Config() = default;
    
    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }
    
    std::unordered_map<std::string, std::string> values_;
    bool loaded_ = false;
};

// Global config accessor
inline Config& config() {
    return Config::instance();
}

} // namespace payoff::config
