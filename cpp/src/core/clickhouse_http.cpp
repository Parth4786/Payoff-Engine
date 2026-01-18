#include "core/clickhouse_http.hpp"
#include "core/config.hpp"
#include "core/http_client.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
    using SOCKET_TYPE = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #define CLOSE_SOCKET close
    #define INVALID_SOCKET -1
    using SOCKET_TYPE = int;
#endif

namespace payoff::core {

namespace {

#ifdef _WIN32
void ensure_winsock_initialized() {
    static std::once_flag once;
    std::call_once(once, []() {
        WSADATA wsa{};
        const int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (rc != 0) {
            throw std::runtime_error("WSAStartup failed: " + std::to_string(rc));
        }
        std::atexit([]() { WSACleanup(); });
    });
}
#endif

std::vector<std::string> parse_tsv_row_internal(const std::string& line) {
    std::vector<std::string> cols;
    std::stringstream ss(line);
    std::string col;
    while (std::getline(ss, col, '\t')) {
        cols.push_back(col);
    }
    return cols;
}

} // namespace

// ============================================================================
// Config Factory
// ============================================================================

ClickHouseHttpConfig ClickHouseHttpConfig::from_config() {
    auto& cfg = config::config();
    if (!cfg.is_loaded()) {
        cfg.load();
    }
    
    ClickHouseHttpConfig ch_cfg;
    ch_cfg.host = cfg.ch_host();
    ch_cfg.port = static_cast<uint16_t>(cfg.ch_port());
    ch_cfg.database = cfg.ch_database();
    ch_cfg.username = cfg.ch_username();
    ch_cfg.password = cfg.ch_password();
    return ch_cfg;
}

// ============================================================================
// Core Query Execution
// ============================================================================

std::string clickhouse_execute_query(const ClickHouseHttpConfig& cfg, const std::string& query) {
#ifdef _WIN32
    ensure_winsock_initialized();
#endif

    std::ostringstream http_request;

    std::string path = "/?database=" + core::HttpClient::url_encode(cfg.database);
    if (!cfg.username.empty()) {
        path += "&user=" + core::HttpClient::url_encode(cfg.username);
    }
    if (!cfg.password.empty()) {
        path += "&password=" + core::HttpClient::url_encode(cfg.password);
    }

    http_request << "POST " << path << " HTTP/1.1\r\n"
                 << "Host: " << cfg.host << ":" << cfg.port << "\r\n"
                 << "Content-Type: text/plain\r\n"
                 << "Content-Length: " << query.length() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << query;

    SOCKET_TYPE sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        throw std::runtime_error("Failed to create socket");
    }

    struct sockaddr_in server;
    std::memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(cfg.port);

    struct hostent* he = gethostbyname(cfg.host.c_str());
    if (!he) {
        CLOSE_SOCKET(sock);
        throw std::runtime_error("Failed to resolve hostname: " + cfg.host);
    }

    std::memcpy(&server.sin_addr, he->h_addr_list[0], static_cast<size_t>(he->h_length));

    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&server), sizeof(server)) < 0) {
        CLOSE_SOCKET(sock);
        throw std::runtime_error("Failed to connect to ClickHouse");
    }

    const std::string req = http_request.str();
    send(sock, req.c_str(), static_cast<int>(req.length()), 0);

    std::string response;
    char buffer[4096];
    int bytes;

    while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';
        response += buffer;
    }

    CLOSE_SOCKET(sock);

    // Validate status line if present
    auto status_end = response.find("\r\n");
    if (status_end != std::string::npos) {
        std::string status_line = response.substr(0, status_end);
        auto first_sp = status_line.find(' ');
        if (first_sp != std::string::npos) {
            auto second_sp = status_line.find(' ', first_sp + 1);
            std::string code_str = (second_sp == std::string::npos)
                ? status_line.substr(first_sp + 1)
                : status_line.substr(first_sp + 1, second_sp - (first_sp + 1));
            try {
                int code = std::stoi(code_str);
                if (code < 200 || code >= 300) {
                    auto body_start = response.find("\r\n\r\n");
                    std::string body = (body_start != std::string::npos)
                        ? response.substr(body_start + 4)
                        : response;
                    throw std::runtime_error("ClickHouse HTTP " + std::to_string(code) + ": " + body);
                }
            } catch (const std::runtime_error&) {
                throw;  // Re-throw ClickHouse errors
            } catch (...) {
                // ignore parse issues
            }
        }
    }

    auto body_start = response.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        return response.substr(body_start + 4);
    }

    return response;
}

// ============================================================================
// TSV Parsing Helpers
// ============================================================================

std::vector<std::vector<std::string>> clickhouse_parse_tsv(const std::string& body) {
    std::vector<std::vector<std::string>> rows;
    std::istringstream ss(body);
    std::string line;
    
    while (std::getline(ss, line)) {
        // Handle CRLF
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;
        
        rows.push_back(parse_tsv_row_internal(line));
    }
    
    return rows;
}

size_t clickhouse_query_stream(
    const ClickHouseHttpConfig& cfg,
    const std::string& query,
    ClickHouseRowCallback callback) {
    
    std::string body = clickhouse_execute_query(cfg, query);
    
    size_t count = 0;
    std::istringstream ss(body);
    std::string line;
    
    while (std::getline(ss, line)) {
        // Handle CRLF
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;
        
        auto cols = parse_tsv_row_internal(line);
        if (!callback(cols)) {
            break;  // Callback returned false, stop iteration
        }
        ++count;
    }
    
    return count;
}

// ============================================================================
// Utility Functions
// ============================================================================

bool clickhouse_ping(const ClickHouseHttpConfig& cfg) {
    try {
        std::string result = clickhouse_execute_query(cfg, "SELECT 1");
        return result.find("1") != std::string::npos;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> clickhouse_describe_table(
    const ClickHouseHttpConfig& cfg,
    const std::string& database,
    const std::string& table) {

    ClickHouseHttpConfig cfg2 = cfg;
    cfg2.database = database;

    const std::string query = "DESCRIBE TABLE " + database + "." + table + " FORMAT TabSeparated";
    const std::string body = clickhouse_execute_query(cfg2, query);

    std::vector<std::string> cols;
    std::istringstream ss(body);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        auto parts = parse_tsv_row_internal(line);
        if (!parts.empty() && !parts[0].empty()) {
            cols.push_back(parts[0]);
        }
    }

    return cols;
}

} // namespace payoff::core
