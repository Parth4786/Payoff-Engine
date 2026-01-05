#pragma once
/**
 * @file clickhouse_http.hpp
 * @brief Minimal ClickHouse HTTP client (no external deps)
 */

#include <cstdint>
#include <string>
#include <vector>

namespace payoff::core {

struct ClickHouseHttpConfig {
    std::string host;
    uint16_t port = 8123;
    std::string database;
    std::string username;
    std::string password;
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

} // namespace payoff::core
