#pragma once
/**
 * @file clickhouse_http.hpp
 * @brief Minimal ClickHouse HTTP client (no external deps)
 */

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace payoff::core {

struct ClickHouseHttpConfig {
    std::string host;
    uint16_t port = 8123;
    std::string database;
    std::string username;
    std::string password;
    
    // Create from config singleton
    static ClickHouseHttpConfig from_config();
};

/**
 * @brief Execute a SQL query over ClickHouse HTTP interface.
 * @return Response body (headers stripped). Throws on non-2xx HTTP.
 */
std::string clickhouse_execute_query(const ClickHouseHttpConfig& cfg, const std::string& query);

/**
 * @brief DESCRIBE TABLE and return column names.
 */
std::vector<std::string> clickhouse_describe_table(
    const ClickHouseHttpConfig& cfg,
    const std::string& database,
    const std::string& table);

/**
 * @brief Parse TSV result into rows of string columns.
 */
std::vector<std::vector<std::string>> clickhouse_parse_tsv(const std::string& body);

/**
 * @brief Streaming row callback: returns false to stop iteration.
 */
using ClickHouseRowCallback = std::function<bool(const std::vector<std::string>& cols)>;

/**
 * @brief Execute query and stream rows via callback.
 * @return Number of rows processed.
 */
size_t clickhouse_query_stream(
    const ClickHouseHttpConfig& cfg,
    const std::string& query,
    ClickHouseRowCallback callback);

/**
 * @brief Ping ClickHouse to check connectivity.
 */
bool clickhouse_ping(const ClickHouseHttpConfig& cfg);

} // namespace payoff::core
