#pragma once
/**
 * @file screener_service.hpp
 * @brief Market screener for all-instruments-at-timestamp analysis
 * 
 * Provides macro-to-micro view:
 * - Query ALL instruments at a specific timestamp
 * - Calculate Greeks/IV/P&L for entire market
 * - Rank/filter instruments by any metric
 * - Drill down to single instrument for detailed replay
 * 
 * Backend handles ALL heavy computation - frontend just renders.
 */

#include "core/datasource.hpp"
#include "core/instrument_manager.hpp"
#include "core/models.hpp"
#include "payoff/models.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace payoff::screener {

// ============================================================================
// Screener Data Types
// ============================================================================

/**
 * @brief Market snapshot for a single instrument at a point in time
 */
struct InstrumentSnapshot {
    // Identity
    uint32_t instrument_id;           // exchange_token
    std::string tradingsymbol;
    std::string underlying;           // e.g., "NIFTY", "BANKNIFTY"
    std::string exchange;             // "NFO", "NSE", "BFO"
    core::InstrumentType instrument_type;
    
    // Option-specific
    double strike = 0.0;
    engine::OptionType option_type = engine::OptionType::Call;
    int64_t expiry_ms = 0;            // Unix timestamp of expiry
    double days_to_expiry = 0.0;
    
    // Price data
    double last_price = 0.0;
    double bid_price = 0.0;
    double ask_price = 0.0;
    double mid_price = 0.0;
    double spread = 0.0;
    double spread_pct = 0.0;
    
    // Volume/OI
    int64_t volume = 0;
    int64_t open_interest = 0;
    int64_t oi_change = 0;            // Change from previous session
    
    // Greeks (for options)
    double implied_volatility = 0.0;
    double delta = 0.0;
    double gamma = 0.0;
    double theta = 0.0;
    double vega = 0.0;
    
    // Derived metrics
    double iv_percentile = 0.0;       // IV rank (0-100)
    double moneyness = 0.0;           // (spot - strike) / strike
    bool is_itm = false;
    bool is_atm = false;
    bool is_otm = false;
    
    // Timestamp
    core::Timestamp exchange_timestamp;
};

/**
 * @brief Underlying (index/equity) snapshot
 */
struct UnderlyingSnapshot {
    std::string symbol;               // "NIFTY", "BANKNIFTY"
    double spot_price = 0.0;
    double prev_close = 0.0;
    double change = 0.0;
    double change_pct = 0.0;
    double day_high = 0.0;
    double day_low = 0.0;
    int64_t volume = 0;
    
    // ATM IV
    double atm_iv = 0.0;
    double atm_iv_percentile = 0.0;
    
    // Option chain summary
    int total_calls = 0;
    int total_puts = 0;
    double total_call_oi = 0;
    double total_put_oi = 0;
    double pcr_oi = 0.0;              // Put-Call Ratio by OI
    double pcr_volume = 0.0;          // Put-Call Ratio by Volume
};

/**
 * @brief Full market state at a timestamp
 */
struct MarketScreenerResult {
    core::Timestamp timestamp;
    
    // Underlying snapshots
    std::vector<UnderlyingSnapshot> underlyings;
    
    // All instrument snapshots
    std::vector<InstrumentSnapshot> instruments;
    
    // Statistics
    size_t total_instruments = 0;
    size_t options_count = 0;
    size_t futures_count = 0;
    size_t equities_count = 0;
    
    // Performance
    double query_time_ms = 0.0;
    double calc_time_ms = 0.0;
};

// ============================================================================
// Filter & Sort Options
// ============================================================================

enum class ScreenerSortField {
    InstrumentId,
    Symbol,
    LastPrice,
    Volume,
    OpenInterest,
    OIChange,
    ImpliedVolatility,
    IVPercentile,
    Delta,
    Gamma,
    Theta,
    Vega,
    Spread,
    SpreadPct,
    DaysToExpiry,
    Moneyness
};

enum class SortOrder {
    Ascending,
    Descending
};

struct ScreenerFilter {
    // Exchange filter
    std::vector<std::string> exchanges;       // Empty = all
    
    // Underlying filter
    std::vector<std::string> underlyings;     // Empty = all
    
    // Instrument type filter
    bool include_options = true;
    bool include_futures = true;
    bool include_equities = false;
    
    // Option type filter
    bool include_calls = true;
    bool include_puts = true;
    
    // Expiry filter
    std::optional<int> min_dte;               // Days to expiry
    std::optional<int> max_dte;
    std::optional<int64_t> specific_expiry_ms; // Specific expiry timestamp
    
    // Moneyness filter
    std::optional<double> min_moneyness;      // e.g., -0.05 for 5% OTM
    std::optional<double> max_moneyness;
    bool only_itm = false;
    bool only_atm = false;                    // Within 1% of spot
    bool only_otm = false;
    
    // Volume/OI filter
    std::optional<int64_t> min_volume;
    std::optional<int64_t> min_oi;
    
    // Greek filters
    std::optional<double> min_iv;
    std::optional<double> max_iv;
    std::optional<double> min_delta;
    std::optional<double> max_delta;
    
    // Spread filter (liquidity)
    std::optional<double> max_spread_pct;     // e.g., 0.02 for 2%
    
    // Pagination
    size_t offset = 0;
    size_t limit = 100;
    
    // Sorting
    ScreenerSortField sort_by = ScreenerSortField::Volume;
    SortOrder sort_order = SortOrder::Descending;
};

// ============================================================================
// Replay Request for Drill-Down
// ============================================================================

struct ReplayRequest {
    // Target instrument(s)
    std::vector<uint32_t> instrument_ids;     // exchange_tokens
    
    // Time range
    core::Timestamp start;
    core::Timestamp end;
    
    // Sampling
    int interval_ms = 1000;                   // Snapshot every N ms
    bool include_greeks = true;
    bool include_depth = false;
    
    // Comparison mode
    bool compare_mode = false;                // Track prediction vs actual
    double initial_prediction = 0.0;          // User's prediction at start
};

struct ReplaySnapshot {
    core::Timestamp timestamp;
    uint32_t instrument_id;
    std::string tradingsymbol;
    
    double last_price;
    double bid_price;
    double ask_price;
    int64_t volume;
    int64_t oi;
    
    // Greeks
    double iv = 0.0;
    double delta = 0.0;
    double gamma = 0.0;
    double theta = 0.0;
    double vega = 0.0;
    
    // P&L from start
    double price_change = 0.0;
    double price_change_pct = 0.0;
    
    // Comparison
    double prediction_delta = 0.0;            // actual - predicted
};

struct ReplayResult {
    ReplayRequest request;
    std::vector<ReplaySnapshot> snapshots;
    
    // Summary
    double start_price = 0.0;
    double end_price = 0.0;
    double total_change = 0.0;
    double total_change_pct = 0.0;
    double max_price = 0.0;
    double min_price = 0.0;
    double volatility = 0.0;
    
    size_t total_ticks = 0;
    double duration_ms = 0.0;
};

// ============================================================================
// Callbacks for Streaming
// ============================================================================

using ScreenerProgressCallback = std::function<void(double progress_pct)>;
using ReplayStreamCallback = std::function<void(const ReplaySnapshot& snap)>;

// ============================================================================
// ScreenerService - Main API for Frontend
// ============================================================================

class ScreenerService {
public:
    ScreenerService(
        std::shared_ptr<core::MarketDataSource> data_source,
        std::shared_ptr<core::InstrumentManager> instrument_manager);
    
    ~ScreenerService();
    
    // Non-copyable
    ScreenerService(const ScreenerService&) = delete;
    ScreenerService& operator=(const ScreenerService&) = delete;
    
    // ========================================================================
    // Market Screener
    // ========================================================================
    
    /**
     * @brief Get market state at a specific timestamp
     * 
     * This is the main "macro view" - returns ALL instruments at a point in time.
     * Backend calculates Greeks/IV for everything.
     * 
     * @param timestamp Point in time to query
     * @param filter Optional filters
     * @return MarketScreenerResult with all matching instruments
     */
    MarketScreenerResult get_market_at_timestamp(
        core::Timestamp timestamp,
        const ScreenerFilter& filter = {});
    
    /**
     * @brief Get available timestamps (trading days/times)
     * 
     * Returns list of timestamps where data is available.
     */
    std::vector<core::Timestamp> get_available_timestamps(
        core::Timestamp start,
        core::Timestamp end,
        int sample_interval_seconds = 60);
    
    /**
     * @brief Get available underlyings
     */
    std::vector<std::string> get_available_underlyings();
    
    /**
     * @brief Get available expiries for an underlying
     */
    std::vector<int64_t> get_available_expiries(
        const std::string& underlying,
        core::Timestamp as_of);
    
    // ========================================================================
    // Drill-Down Replay
    // ========================================================================
    
    /**
     * @brief Replay instrument(s) over time range
     * 
     * This is the "micro view" - detailed evolution of specific instruments.
     * 
     * @param request Replay parameters
     * @return ReplayResult with all snapshots
     */
    ReplayResult replay_instruments(const ReplayRequest& request);
    
    /**
     * @brief Stream replay to callback (for WebSocket)
     * 
     * Same as replay_instruments but streams snapshots via callback.
     */
    void stream_replay(
        const ReplayRequest& request,
        ReplayStreamCallback callback,
        ScreenerProgressCallback progress_callback = nullptr);
    
    // ========================================================================
    // Option Chain View
    // ========================================================================
    
    /**
     * @brief Get option chain for underlying at timestamp
     * 
     * Returns structured option chain with calls/puts per strike.
     */
    struct OptionChainEntry {
        double strike;
        InstrumentSnapshot call;
        InstrumentSnapshot put;
        double net_oi;                        // Call OI - Put OI
        double net_volume;
    };
    
    struct OptionChainResult {
        std::string underlying;
        double spot_price;
        int64_t expiry_ms;
        core::Timestamp timestamp;
        std::vector<OptionChainEntry> chain;
        double atm_strike;
        double max_pain;

        // Diagnostics / degradation info (non-breaking for consumers)
        std::string source;                 // "clickhouse" | "instrument_master" | "none"
        std::string warning;                // human-readable if degraded
    };
    
    OptionChainResult get_option_chain(
        const std::string& underlying,
        int64_t expiry_ms,
        core::Timestamp timestamp);
    
    // ========================================================================
    // IV Surface
    // ========================================================================
    
    struct IVSurfacePoint {
        double strike;
        double dte;
        double iv;
    };
    
    struct IVSurfaceResult {
        std::string underlying;
        double spot_price;
        core::Timestamp timestamp;
        std::vector<IVSurfacePoint> surface;
    };
    
    IVSurfaceResult get_iv_surface(
        const std::string& underlying,
        core::Timestamp timestamp);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<ScreenerService> create_screener_service();

} // namespace payoff::screener
