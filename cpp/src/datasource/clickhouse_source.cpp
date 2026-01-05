/**
 * @file clickhouse_source.cpp
 * @brief ClickHouse data source - full HTTP API implementation
 * 
 * Uses HTTP interface for:
 * - Historical replay (deterministic ordering)
 * - Live polling (watermark pattern)
 */

#include "core/datasource.hpp"
#include "core/config.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

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

// Parse tab-separated value row
std::vector<std::string> parse_tsv_row(const std::string& line) {
    std::vector<std::string> cols;
    std::stringstream ss(line);
    std::string col;
    while (std::getline(ss, col, '\t')) {
        cols.push_back(col);
    }
    return cols;
}

// Convert milliseconds timestamp to string for SQL
std::string ms_to_sql_datetime(int64_t ms) {
    time_t sec = static_cast<time_t>(ms / 1000);
    std::tm tm = *std::gmtime(&sec);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

} // anonymous namespace

// ============================================================================
// ClickHouseSource - Full Implementation
// ============================================================================

class ClickHouseSource : public MarketDataSource {
public:
    ClickHouseSource(const std::string& host, uint16_t port, 
                     const std::string& database, 
                     const std::string& username = "default",
                     const std::string& password = "")
        : host_(host)
        , port_(port)
        , database_(database)
        , username_(username)
        , password_(password) {
        
        // Get table name from config
        table_ = config::config().ch_table();
        if (table_.empty()) {
            table_ = "market_data";
        }
        
        // Test connection
        connected_ = ping();
    }
    
    Source get_source_type() const noexcept override {
        return Source::ClickHouse;
    }
    
    std::string get_name() const override {
        return "ClickHouse:" + host_ + ":" + std::to_string(port_) + "/" + database_;
    }
    
    bool is_connected() const noexcept override {
        return connected_;
    }
    
    std::vector<std::string> get_symbols() const override {
        std::vector<std::string> symbols;
        
        std::string query = "SELECT DISTINCT instrument_token FROM " + 
                            database_ + "." + table_ + " LIMIT 1000";
        
        std::string result = execute_query(query);
        
        std::istringstream ss(result);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty()) {
                symbols.push_back(line);
            }
        }
        
        return symbols;
    }
    
    bool subscribe(const std::vector<std::string>& symbols) override {
        std::lock_guard<std::mutex> lock(subscription_mutex_);
        for (const auto& sym : symbols) {
            subscribed_symbols_.insert(sym);
        }
        return true;
    }
    
    void unsubscribe(const std::vector<std::string>& symbols) override {
        std::lock_guard<std::mutex> lock(subscription_mutex_);
        for (const auto& sym : symbols) {
            subscribed_symbols_.erase(sym);
        }
    }
    
    size_t replay_snapshots(
        const std::vector<std::string>& symbols,
        const TimeRange& range,
        SnapshotCallback callback) override {
        
        if (!connected_) {
            throw std::runtime_error("Not connected to ClickHouse");
        }
        
        // Build query
        std::ostringstream query;
        query << "SELECT instrument_token, tradingsymbol, "
              << "toUnixTimestamp64Milli(exchange_timestamp) as ts_ms, "
              << "last_price, volume, "
              << "bid_price, bid_quantity, ask_price, ask_quantity, "
              << "open_interest "
              << "FROM " << database_ << "." << table_ << " "
              << "WHERE exchange_timestamp >= '" << ms_to_sql_datetime(range.start.count()) << "' "
              << "AND exchange_timestamp <= '" << ms_to_sql_datetime(range.end.count()) << "' ";
        
        if (!symbols.empty()) {
            query << "AND instrument_token IN (";
            for (size_t i = 0; i < symbols.size(); ++i) {
                if (i > 0) query << ",";
                query << symbols[i];
            }
            query << ") ";
        }
        
        query << "ORDER BY exchange_timestamp ASC "
              << "FORMAT TabSeparated";
        
        std::string result = execute_query(query.str());
        
        // Parse results and invoke callback
        size_t count = 0;
        std::istringstream ss(result);
        std::string line;
        
        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            
            auto cols = parse_tsv_row(line);
            if (cols.size() < 10) continue;
            
            DepthSnapshot snap = parse_row_to_snapshot(cols);
            callback(snap);
            count++;
        }
        
        return count;
    }
    
    void start_streaming(
        SnapshotCallback callback,
        ErrorCallback error_callback) override {
        
        if (streaming_) {
            return;
        }
        
        streaming_ = true;
        
        // Get initial watermark
        watermark_ = get_max_timestamp();
        
        // Start polling thread
        poll_thread_ = std::make_unique<std::thread>([this, callback, error_callback]() {
            int poll_interval = config::config().ch_poll_interval_ms();
            int batch_size = config::config().ch_batch_size();
            
            while (streaming_) {
                try {
                    // Query for new data since watermark
                    std::ostringstream query;
                    query << "SELECT instrument_token, tradingsymbol, "
                          << "toUnixTimestamp64Milli(exchange_timestamp) as ts_ms, "
                          << "last_price, volume, "
                          << "bid_price, bid_quantity, ask_price, ask_quantity, "
                          << "open_interest "
                          << "FROM " << database_ << "." << table_ << " "
                          << "WHERE toUnixTimestamp64Milli(exchange_timestamp) > " << watermark_ << " ";
                    
                    // Filter by subscribed symbols if any
                    {
                        std::lock_guard<std::mutex> lock(subscription_mutex_);
                        if (!subscribed_symbols_.empty()) {
                            query << "AND instrument_token IN (";
                            bool first = true;
                            for (const auto& sym : subscribed_symbols_) {
                                if (!first) query << ",";
                                query << sym;
                                first = false;
                            }
                            query << ") ";
                        }
                    }
                    
                    query << "ORDER BY exchange_timestamp ASC "
                          << "LIMIT " << batch_size << " "
                          << "FORMAT TabSeparated";
                    
                    std::string result = execute_query(query.str());
                    
                    // Parse and dispatch
                    std::istringstream ss(result);
                    std::string line;
                    
                    while (std::getline(ss, line)) {
                        if (line.empty()) continue;
                        
                        auto cols = parse_tsv_row(line);
                        if (cols.size() < 10) continue;
                        
                        DepthSnapshot snap = parse_row_to_snapshot(cols);
                        
                        // Update watermark
                        int64_t ts_ms = snap.exchange_timestamp.count();
                        if (ts_ms > watermark_) {
                            watermark_ = ts_ms;
                        }
                        
                        callback(snap);
                    }
                    
                } catch (const std::exception& e) {
                    if (error_callback) {
                        error_callback(e.what());
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval));
            }
        });
    }
    
    void stop_streaming() override {
        streaming_ = false;
        
        if (poll_thread_ && poll_thread_->joinable()) {
            poll_thread_->join();
        }
    }
    
    bool is_streaming() const noexcept override {
        return streaming_;
    }

private:
    std::string host_;
    uint16_t port_;
    std::string database_;
    std::string username_;
    std::string password_;
    std::string table_;
    
    bool connected_ = false;
    std::atomic<bool> streaming_{false};
    std::atomic<int64_t> watermark_{0};
    
    std::unique_ptr<std::thread> poll_thread_;
    
    std::set<std::string> subscribed_symbols_;
    mutable std::mutex subscription_mutex_;
    
    // ========================================================================
    // HTTP Client
    // ========================================================================
    
    bool ping() const {
        try {
            std::string result = execute_query("SELECT 1");
            return result.find("1") != std::string::npos;
        } catch (...) {
            return false;
        }
    }
    
    int64_t get_max_timestamp() const {
        std::string query = "SELECT max(toUnixTimestamp64Milli(exchange_timestamp)) FROM " +
                            database_ + "." + table_;
        std::string result = execute_query(query);
        
        try {
            return std::stoll(result);
        } catch (...) {
            return 0;
        }
    }
    
    std::string execute_query(const std::string& query) const {
        // Build HTTP POST request to ClickHouse
        std::ostringstream http_request;
        
        // URL encode query for safety
        std::string path = "/?database=" + database_;
        if (!username_.empty()) {
            path += "&user=" + username_;
        }
        if (!password_.empty()) {
            path += "&password=" + password_;
        }
        
        http_request << "POST " << path << " HTTP/1.1\r\n"
                     << "Host: " << host_ << ":" << port_ << "\r\n"
                     << "Content-Type: text/plain\r\n"
                     << "Content-Length: " << query.length() << "\r\n"
                     << "Connection: close\r\n"
                     << "\r\n"
                     << query;
        
        // Create socket and connect
        SOCKET_TYPE sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            throw std::runtime_error("Failed to create socket");
        }
        
        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_port = htons(port_);
        
        // Resolve hostname
        struct hostent* he = gethostbyname(host_.c_str());
        if (!he) {
            CLOSE_SOCKET(sock);
            throw std::runtime_error("Failed to resolve hostname: " + host_);
        }
        
        memcpy(&server.sin_addr, he->h_addr_list[0], static_cast<size_t>(he->h_length));
        
        if (::connect(sock, reinterpret_cast<struct sockaddr*>(&server), sizeof(server)) < 0) {
            CLOSE_SOCKET(sock);
            throw std::runtime_error("Failed to connect to ClickHouse");
        }
        
        // Send request
        std::string req = http_request.str();
        send(sock, req.c_str(), static_cast<int>(req.length()), 0);
        
        // Receive response
        std::string response;
        char buffer[4096];
        int bytes;
        
        while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes] = '\0';
            response += buffer;
        }
        
        CLOSE_SOCKET(sock);
        
        // Parse HTTP response - extract body after headers
        auto body_start = response.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            return response.substr(body_start + 4);
        }
        
        return response;
    }
    
    DepthSnapshot parse_row_to_snapshot(const std::vector<std::string>& cols) const {
        DepthSnapshot snap;
        
        // Expected columns:
        // 0: instrument_token
        // 1: tradingsymbol
        // 2: ts_ms
        // 3: last_price
        // 4: volume
        // 5: bid_price
        // 6: bid_quantity
        // 7: ask_price
        // 8: ask_quantity
        // 9: open_interest
        
        try {
            snap.instrument_id = static_cast<uint32_t>(std::stoul(cols[0]));
            snap.symbol = cols[1];
            
            int64_t ts_ms = std::stoll(cols[2]);
            snap.exchange_timestamp = Timestamp(ts_ms);
            snap.receive_timestamp = Timestamp(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            
            snap.trade.last_price = std::stod(cols[3]);
            snap.trade.total_traded_quantity = std::stoll(cols[4]);
            snap.trade.open_interest = std::stoll(cols[9]);
            
            // Best bid/ask
            double bid_price = std::stod(cols[5]);
            int64_t bid_qty = std::stoll(cols[6]);
            double ask_price = std::stod(cols[7]);
            int64_t ask_qty = std::stoll(cols[8]);
            
            if (bid_price > 0) {
                snap.bids.push_back({bid_price, bid_qty, 1});
            }
            if (ask_price > 0) {
                snap.asks.push_back({ask_price, ask_qty, 1});
            }
            
            snap.source = Source::ClickHouse;
            
        } catch (const std::exception& e) {
            // Invalid row, return empty snapshot
        }
        
        return snap;
    }
};

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<MarketDataSource> create_clickhouse_source(
    const std::string& host,
    uint16_t port,
    const std::string& database) {
    
    auto& cfg = config::config();
    return std::make_unique<ClickHouseSource>(
        host, port, database,
        cfg.ch_username(), cfg.ch_password());
}

std::unique_ptr<MarketDataSource> create_clickhouse_source_from_config() {
    auto& cfg = config::config();
    if (!cfg.is_loaded()) {
        cfg.load();
    }
    
    return std::make_unique<ClickHouseSource>(
        cfg.ch_host(),
        static_cast<uint16_t>(cfg.ch_port()),
        cfg.ch_database(),
        cfg.ch_username(),
        cfg.ch_password());
}

} // namespace payoff::core
