#pragma once
/**
 * @file clickhouse_client.hpp
 * @brief ClickHouse HTTP API client
 * 
 * Connects to ClickHouse via HTTP interface for:
 * - Querying tick/bar data
 * - Replay historical data
 * - Polling for live updates
 */

#include "core/config.hpp"
#include "core/models.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace payoff::clickhouse {

// ============================================================================
// Query Result
// ============================================================================

enum class CHError {
    None = 0,
    ConnectionFailed,
    QueryFailed,
    ParseError,
    Timeout
};

struct CHResult {
    CHError error = CHError::None;
    std::string message;
    
    [[nodiscard]] bool ok() const noexcept { return error == CHError::None; }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
};

// ============================================================================
// Row Data
// ============================================================================

struct TickRow {
    uint32_t instrument_token = 0;
    std::string symbol;
    int64_t exchange_timestamp_ms = 0;
    double last_price = 0.0;
    int64_t volume = 0;
    double bid_price = 0.0;
    int64_t bid_qty = 0;
    double ask_price = 0.0;
    int64_t ask_qty = 0;
    int64_t oi = 0;
    
    [[nodiscard]] core::DepthSnapshot to_snapshot() const;
};

struct BarRow {
    uint32_t instrument_token = 0;
    std::string symbol;
    int64_t timestamp_ms = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    int64_t volume = 0;
};

// ============================================================================
// ClickHouse Client
// ============================================================================

class ClickHouseClient {
public:
    /**
     * @brief Construct with connection parameters
     */
    ClickHouseClient(const std::string& host, int port,
                     const std::string& database,
                     const std::string& username = "default",
                     const std::string& password = "");
    
    /**
     * @brief Construct from config
     */
    ClickHouseClient();
    
    ~ClickHouseClient();
    
    // ========================================================================
    // Connection
    // ========================================================================
    
    /**
     * @brief Test connection to server
     */
    [[nodiscard]] bool ping();
    
    /**
     * @brief Check if connected
     */
    [[nodiscard]] bool is_connected() const noexcept { return connected_; }
    
    /**
     * @brief Get server version
     */
    [[nodiscard]] std::string get_version();
    
    // ========================================================================
    // Query Execution
    // ========================================================================
    
    /**
     * @brief Execute raw SQL query and get string result
     */
    [[nodiscard]] std::string execute(const std::string& query);
    
    /**
     * @brief Execute query and parse as JSON
     */
    [[nodiscard]] std::string execute_json(const std::string& query);
    
    // ========================================================================
    // Market Data Queries
    // ========================================================================
    
    /**
     * @brief Query ticks for a symbol in time range
     */
    [[nodiscard]] std::vector<TickRow> query_ticks(
        uint32_t instrument_token,
        int64_t start_ms,
        int64_t end_ms,
        size_t limit = 10000);
    
    /**
     * @brief Query ticks for multiple symbols
     */
    [[nodiscard]] std::vector<TickRow> query_ticks_multi(
        const std::vector<uint32_t>& instrument_tokens,
        int64_t start_ms,
        int64_t end_ms,
        size_t limit = 10000);
    
    /**
     * @brief Query bars at specific interval
     */
    [[nodiscard]] std::vector<BarRow> query_bars(
        uint32_t instrument_token,
        int64_t start_ms,
        int64_t end_ms,
        const std::string& interval = "1m");
    
    /**
     * @brief Get latest tick for a symbol
     */
    [[nodiscard]] std::optional<TickRow> get_latest_tick(uint32_t instrument_token);
    
    /**
     * @brief Get max timestamp (watermark) for polling
     */
    [[nodiscard]] int64_t get_max_timestamp();
    
    // ========================================================================
    // Streaming (Polling Pattern)
    // ========================================================================
    
    using TickCallback = std::function<void(const TickRow&)>;
    
    /**
     * @brief Start polling for new data
     * @param callback Called for each new tick
     * @param instrument_tokens Tokens to poll (empty = all)
     * @param poll_interval_ms Polling interval
     */
    void start_polling(
        TickCallback callback,
        const std::vector<uint32_t>& instrument_tokens = {},
        int poll_interval_ms = 50);
    
    /**
     * @brief Stop polling
     */
    void stop_polling();
    
    /**
     * @brief Check if polling
     */
    [[nodiscard]] bool is_polling() const noexcept { return polling_; }
    
    // ========================================================================
    // Getters
    // ========================================================================
    
    [[nodiscard]] const std::string& host() const noexcept { return host_; }
    [[nodiscard]] int port() const noexcept { return port_; }
    [[nodiscard]] const std::string& database() const noexcept { return database_; }
    [[nodiscard]] CHError last_error() const noexcept { return last_error_; }
    [[nodiscard]] const std::string& last_error_message() const noexcept { return last_error_message_; }

private:
    std::string host_;
    int port_;
    std::string database_;
    std::string username_;
    std::string password_;
    std::string table_;
    
    bool connected_ = false;
    bool polling_ = false;
    
    CHError last_error_ = CHError::None;
    std::string last_error_message_;
    
    // Watermark for polling
    int64_t last_watermark_ = 0;
    
    // HTTP request helper
    std::string http_post(const std::string& query);
    void set_error(CHError err, const std::string& msg);
    void clear_error();
    
    // Row parsing
    TickRow parse_tick_row(const std::string& line);
    BarRow parse_bar_row(const std::string& line);
};

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<ClickHouseClient> create_clickhouse_client();

} // namespace payoff::clickhouse
