/**
 * @file clickhouse_source.cpp
 * @brief ClickHouse data source - uses shared HTTP helper
 * 
 * Uses HTTP interface for:
 * - Historical replay (deterministic ordering)
 * - Live polling (watermark pattern)
 */

#include "core/datasource.hpp"
#include "core/config.hpp"
#include "core/clickhouse_http.hpp"
#include "core/instrument_manager.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace payoff::core {

namespace {

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
// ClickHouseSource - Full Implementation (uses shared HTTP helper)
// ============================================================================

class ClickHouseSource : public MarketDataSource {
public:
    ClickHouseSource(const std::string& host, uint16_t port, 
                     const std::string& database, 
                     const std::string& username = "default",
                     const std::string& password = "")
        : instrument_manager_(nullptr) {
        
        // Configure HTTP client
        ch_config_.host = host;
        ch_config_.port = port;
        ch_config_.database = database;
        ch_config_.username = username;
        ch_config_.password = password;
        
        // Get table name from config
        table_ = config::config().ch_table();
        if (table_.empty()) {
            table_ = "market_data";
        }
        
        // Test connection
        connected_ = clickhouse_ping(ch_config_);
    }
    
    /**
     * @brief Set instrument manager for exchange_token resolution
     */
    void set_instrument_manager(std::shared_ptr<InstrumentManager> mgr) {
        instrument_manager_ = std::move(mgr);
    }
    
    Source get_source_type() const noexcept override {
        return Source::ClickHouse;
    }
    
    std::string get_name() const override {
        return "ClickHouse:" + ch_config_.host + ":" + 
               std::to_string(ch_config_.port) + "/" + ch_config_.database;
    }
    
    bool is_connected() const noexcept override {
        return connected_;
    }
    
    std::vector<std::string> get_symbols() const override {
        std::vector<std::string> symbols;
        
        std::string query = "SELECT DISTINCT instrument_id FROM " + 
                            ch_config_.database + "." + table_ + " LIMIT 1000 FORMAT TabSeparated";
        
        try {
            std::string result = clickhouse_execute_query(ch_config_, query);
            
            std::istringstream ss(result);
            std::string line;
            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) {
                    // Resolve exchange_token to canonical symbol using InstrumentManager
                    if (instrument_manager_) {
                        try {
                            uint32_t exchange_token = static_cast<uint32_t>(std::stoul(line));
                            if (auto* inst = instrument_manager_->resolve(exchange_token)) {
                                symbols.push_back(inst->canonical_symbol());
                            } else {
                                // Fallback: use raw token as symbol
                                symbols.push_back(line);
                            }
                        } catch (...) {
                            symbols.push_back(line);
                        }
                    } else {
                        symbols.push_back(line);
                    }
                }
            }
        } catch (...) {
            // Return empty on error
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
        
        // Build query - note: instrument_id in ClickHouse = exchange_token
        std::ostringstream query;
        query << "SELECT instrument_id, tradingsymbol, "
              << "toUnixTimestamp64Milli(exchange_timestamp) as ts_ms, "
              << "last_price, volume, "
              << "bid_price, bid_quantity, ask_price, ask_quantity, "
              << "open_interest "
              << "FROM " << ch_config_.database << "." << table_ << " "
              << "WHERE exchange_timestamp >= '" << ms_to_sql_datetime(range.start.count()) << "' "
              << "AND exchange_timestamp <= '" << ms_to_sql_datetime(range.end.count()) << "' ";
        
        if (!symbols.empty()) {
            // Resolve canonical symbols to exchange_tokens
            std::vector<uint32_t> exchange_tokens;
            for (const auto& sym : symbols) {
                // Handle both raw tokens and canonical symbols
                if (sym.find(':') != std::string::npos && instrument_manager_) {
                    // Canonical format "NFO:49543" - extract token
                    auto pos = sym.find(':');
                    try {
                        uint32_t token = static_cast<uint32_t>(std::stoul(sym.substr(pos + 1)));
                        exchange_tokens.push_back(token);
                    } catch (...) {}
                } else {
                    // Try as raw token
                    try {
                        exchange_tokens.push_back(static_cast<uint32_t>(std::stoul(sym)));
                    } catch (...) {}
                }
            }
            
            if (!exchange_tokens.empty()) {
                query << "AND instrument_id IN (";
                for (size_t i = 0; i < exchange_tokens.size(); ++i) {
                    if (i > 0) query << ",";
                    query << exchange_tokens[i];
                }
                query << ") ";
            }
        }
        
        query << "ORDER BY exchange_timestamp ASC "
              << "FORMAT TabSeparated";
        
        // Stream results via shared helper
        size_t count = 0;
        clickhouse_query_stream(ch_config_, query.str(), 
            [&](const std::vector<std::string>& cols) {
                if (cols.size() < 10) return true;  // Skip invalid rows
                
                DepthSnapshot snap = parse_row_to_snapshot(cols);
                callback(snap);
                count++;
                return true;  // Continue iteration
            });
        
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
                    query << "SELECT instrument_id, tradingsymbol, "
                          << "toUnixTimestamp64Milli(exchange_timestamp) as ts_ms, "
                          << "last_price, volume, "
                          << "bid_price, bid_quantity, ask_price, ask_quantity, "
                          << "open_interest "
                          << "FROM " << ch_config_.database << "." << table_ << " "
                          << "WHERE toUnixTimestamp64Milli(exchange_timestamp) > " << watermark_ << " ";
                    
                    // Filter by subscribed symbols (as exchange_tokens)
                    {
                        std::lock_guard<std::mutex> lock(subscription_mutex_);
                        if (!subscribed_symbols_.empty()) {
                            query << "AND instrument_id IN (";
                            bool first = true;
                            for (const auto& sym : subscribed_symbols_) {
                                // Extract exchange_token from canonical symbol
                                std::string token_str = sym;
                                if (sym.find(':') != std::string::npos) {
                                    token_str = sym.substr(sym.find(':') + 1);
                                }
                                if (!first) query << ",";
                                query << token_str;
                                first = false;
                            }
                            query << ") ";
                        }
                    }
                    
                    query << "ORDER BY exchange_timestamp ASC "
                          << "LIMIT " << batch_size << " "
                          << "FORMAT TabSeparated";
                    
                    // Stream using shared helper
                    clickhouse_query_stream(ch_config_, query.str(),
                        [&](const std::vector<std::string>& cols) {
                            if (cols.size() < 10) return true;
                            
                            DepthSnapshot snap = parse_row_to_snapshot(cols);
                            
                            // Update watermark
                            int64_t ts_ms = snap.exchange_timestamp.count();
                            if (ts_ms > watermark_) {
                                watermark_ = ts_ms;
                            }
                            
                            callback(snap);
                            return true;
                        });
                    
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
    ClickHouseHttpConfig ch_config_;
    std::string table_;
    std::shared_ptr<InstrumentManager> instrument_manager_;
    
    bool connected_ = false;
    std::atomic<bool> streaming_{false};
    std::atomic<int64_t> watermark_{0};
    
    std::unique_ptr<std::thread> poll_thread_;
    
    std::set<std::string> subscribed_symbols_;
    mutable std::mutex subscription_mutex_;
    
    // ========================================================================
    // Helper Methods
    // ========================================================================
    
    int64_t get_max_timestamp() const {
        std::string query = "SELECT max(toUnixTimestamp64Milli(exchange_timestamp)) FROM " +
                            ch_config_.database + "." + table_ + " FORMAT TabSeparated";
        try {
            std::string result = clickhouse_execute_query(ch_config_, query);
            // Trim whitespace
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
                result.pop_back();
            }
            return std::stoll(result);
        } catch (...) {
            return 0;
        }
    }
    
    DepthSnapshot parse_row_to_snapshot(const std::vector<std::string>& cols) const {
        DepthSnapshot snap;
        
        // Expected columns:
        // 0: instrument_id (= exchange_token in our model)
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
            // instrument_id from ClickHouse IS the exchange_token
            uint32_t exchange_token = static_cast<uint32_t>(std::stoul(cols[0]));
            snap.instrument_id = exchange_token;
            
            // Build canonical symbol using InstrumentManager if available
            if (instrument_manager_) {
                if (auto* inst = instrument_manager_->resolve(exchange_token)) {
                    snap.symbol = inst->canonical_symbol();
                } else {
                    // Fallback: construct from tradingsymbol
                    snap.symbol = "UNKNOWN:" + std::to_string(exchange_token);
                }
            } else {
                snap.symbol = cols[1];  // Use tradingsymbol from data
            }
            
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
// Factory Functions
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
    auto ch_cfg = ClickHouseHttpConfig::from_config();
    
    return std::make_unique<ClickHouseSource>(
        ch_cfg.host,
        ch_cfg.port,
        ch_cfg.database,
        ch_cfg.username,
        ch_cfg.password);
}

} // namespace payoff::core
