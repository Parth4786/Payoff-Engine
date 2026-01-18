#pragma once
/**
 * @file models.hpp
 * @brief Core data models for the Payoff Engine
 * 
 * This file defines the canonical data contracts:
 * - DepthLevel: Single price level in order book
 * - TradeInfo: Last trade and volume information
 * - DepthSnapshot: Complete market snapshot (normalized from all sources)
 * 
 * NON-NEGOTIABLES:
 * - bids MUST be sorted DESC (best bid first)
 * - asks MUST be sorted ASC (best ask first)
 * - best_bid < best_ask (crossed book is invalid)
 * - instrument_id = exchange_token (never guess)
 */

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>

namespace payoff::core {

// ============================================================================
// Time Types
// ============================================================================
using Timestamp = std::chrono::milliseconds;
using Clock = std::chrono::system_clock;
using TimePoint = std::chrono::time_point<Clock>;

// ============================================================================
// Data Source Enum
// ============================================================================
enum class Source : uint8_t {
    Unknown = 0,
    ClickHouse = 1,    // Historical replay
    KiteWS = 2,        // Live WebSocket
    Mock = 3           // Test data
};

constexpr const char* source_to_string(Source s) {
    switch (s) {
        case Source::ClickHouse: return "clickhouse";
        case Source::KiteWS: return "kite_ws";
        case Source::Mock: return "mock";
        default: return "unknown";
    }
}

// ============================================================================
// DepthLevel - Single price level in order book
// ============================================================================
struct DepthLevel {
    double price = 0.0;
    int64_t size = 0;      // Quantity at this level
    int32_t orders = 0;    // Number of orders at this level
    
    constexpr bool is_valid() const noexcept {
        return price > 0.0 && size >= 0 && orders >= 0;
    }
    
    constexpr bool operator==(const DepthLevel& other) const noexcept {
        return price == other.price && size == other.size && orders == other.orders;
    }
};

// ============================================================================
// TradeInfo - Last trade and aggregate volume info
// ============================================================================
struct TradeInfo {
    double last_price = 0.0;
    int64_t last_traded_quantity = 0;
    int64_t total_traded_quantity = 0;    // Total volume for the day
    int64_t total_buy_quantity = 0;
    int64_t total_sell_quantity = 0;
    
    // Open Interest - ONLY available from Kite WebSocket live stream
    // ClickHouse historical data does NOT have OI
    int64_t open_interest = 0;
    int64_t oi_day_high = 0;              // Kite FULL mode only
    int64_t oi_day_low = 0;               // Kite FULL mode only
    
    // Average traded price
    double average_traded_price = 0.0;
    
    // OHLC for the day
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    
    // Timestamps
    std::optional<Timestamp> last_trade_time;
    std::optional<Timestamp> exchange_timestamp;
    
    constexpr bool has_trade() const noexcept {
        return last_price > 0.0;
    }
    
    constexpr bool has_oi() const noexcept {
        return open_interest > 0;
    }
};

// ============================================================================
// DepthSnapshot - Complete market snapshot (canonical internal model)
// ============================================================================
struct DepthSnapshot {
    // Identity
    uint32_t instrument_id = 0;    // = exchange_token (NEVER guess this)
    std::string symbol;            // Canonical format: "NFO:49543"
    
    // Timing
    Timestamp exchange_timestamp{0};
    Timestamp receive_timestamp{0};
    
    // Order book - INVARIANT: bids DESC, asks ASC
    std::vector<DepthLevel> bids;  // Best (highest) first
    std::vector<DepthLevel> asks;  // Best (lowest) first
    
    // Trade info
    TradeInfo trade;
    
    // Metadata
    Source source = Source::Unknown;
    bool is_partial = false;       // Only bids OR asks updated (not full book)
    bool is_stale = false;         // Data older than threshold
    
    // ========================================================================
    // Validation
    // ========================================================================
    
    /**
     * @brief Validate snapshot invariants
     * @throws std::runtime_error if invariants violated
     */
    void validate() const;
    
    /**
     * @brief Check if snapshot is valid without throwing
     */
    [[nodiscard]] bool is_valid() const noexcept;
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    [[nodiscard]] std::optional<double> best_bid() const noexcept {
        if (bids.empty()) return std::nullopt;
        return bids.front().price;
    }
    
    [[nodiscard]] std::optional<double> best_ask() const noexcept {
        if (asks.empty()) return std::nullopt;
        return asks.front().price;
    }
    
    [[nodiscard]] std::optional<double> midprice() const noexcept {
        auto bb = best_bid();
        auto ba = best_ask();
        if (!bb || !ba) return std::nullopt;
        return (*bb + *ba) / 2.0;
    }
    
    [[nodiscard]] std::optional<double> spread() const noexcept {
        auto bb = best_bid();
        auto ba = best_ask();
        if (!bb || !ba) return std::nullopt;
        return *ba - *bb;
    }
    
    [[nodiscard]] int64_t total_bid_size() const noexcept {
        int64_t total = 0;
        for (const auto& level : bids) total += level.size;
        return total;
    }
    
    [[nodiscard]] int64_t total_ask_size() const noexcept {
        int64_t total = 0;
        for (const auto& level : asks) total += level.size;
        return total;
    }
};

// ============================================================================
// Validation Exception
// ============================================================================
class ValidationError : public std::runtime_error {
public:
    explicit ValidationError(const std::string& msg) 
        : std::runtime_error(msg) {}
};

// ============================================================================
// Factory / Builder
// ============================================================================
class DepthSnapshotBuilder {
public:
    DepthSnapshotBuilder& instrument_id(uint32_t id);
    DepthSnapshotBuilder& symbol(std::string sym);
    DepthSnapshotBuilder& exchange_timestamp(Timestamp ts);
    DepthSnapshotBuilder& receive_timestamp(Timestamp ts);
    DepthSnapshotBuilder& add_bid(double price, int64_t size, int32_t orders = 1);
    DepthSnapshotBuilder& add_ask(double price, int64_t size, int32_t orders = 1);
    DepthSnapshotBuilder& trade_info(TradeInfo info);
    DepthSnapshotBuilder& source(Source src);
    DepthSnapshotBuilder& partial(bool p = true);
    DepthSnapshotBuilder& stale(bool s = true);
    
    /**
     * @brief Build and validate the snapshot
     * @throws ValidationError if invariants violated
     */
    [[nodiscard]] DepthSnapshot build();
    
    /**
     * @brief Build without validation (use carefully)
     */
    [[nodiscard]] DepthSnapshot build_unchecked() noexcept;

private:
    DepthSnapshot snapshot_;
};

} // namespace payoff::core
