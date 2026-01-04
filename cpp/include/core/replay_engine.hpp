#pragma once
/**
 * @file replay_engine.hpp
 * @brief Replay engine for historical payoff analysis
 * 
 * Uses ClickHouse historical data to:
 * - Verify payoff calculations at any point in time
 * - Backtest strategies with actual market data
 * - Analyze what-if scenarios using real price movements
 */

#include "core/datasource.hpp"
#include "core/instrument_manager.hpp"
#include "core/models.hpp"
#include "payoff/calculator.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace payoff::core {

// ============================================================================
// Replay Configuration
// ============================================================================

struct ReplayConfig {
    TimeRange time_range;
    std::vector<std::string> symbols;         // Canonical symbols
    
    // Replay speed (1.0 = real-time, 0 = as-fast-as-possible)
    double speed_multiplier = 0.0;
    
    // Sampling
    int sample_interval_ms = 100;             // Aggregate ticks within this interval
    bool include_depth = true;                // Include order book depth
    
    // Output
    bool emit_payoff_at_each_tick = false;    // Calculate payoff at every tick
    int payoff_sample_interval_ms = 1000;     // Calculate payoff every N ms
};

// ============================================================================
// Replay Event Types
// ============================================================================

struct ReplayTick {
    Timestamp timestamp;
    uint32_t instrument_token;
    std::string symbol;
    
    double last_price;
    double bid_price;
    double ask_price;
    int64_t volume;
    int64_t oi;
    
    // Depth if available
    std::vector<DepthLevel> bids;
    std::vector<DepthLevel> asks;
};

struct ReplayPayoffSnapshot {
    Timestamp timestamp;
    double underlying_price;
    
    // Strategy P&L
    double total_pnl;
    double unrealized_pnl;
    double realized_pnl;
    
    // Greeks
    engine::Greeks portfolio_greeks;
    
    // Individual leg P&L
    std::vector<double> leg_pnls;
    
    // Risk metrics
    double max_profit_remaining;
    double max_loss_remaining;
    double probability_of_profit;
};

// ============================================================================
// Callbacks
// ============================================================================

using ReplayTickCallback = std::function<void(const ReplayTick&)>;
using ReplayPayoffCallback = std::function<void(const ReplayPayoffSnapshot&)>;
using ReplayProgressCallback = std::function<void(double progress_pct, Timestamp current)>;
using ReplayCompleteCallback = std::function<void(size_t ticks_processed, size_t snapshots_generated)>;

// ============================================================================
// PayoffReplayEngine - Main replay orchestrator
// ============================================================================

class PayoffReplayEngine {
public:
    PayoffReplayEngine(
        std::shared_ptr<MarketDataSource> data_source,
        std::shared_ptr<InstrumentManager> instrument_manager);
    
    ~PayoffReplayEngine();
    
    // Non-copyable
    PayoffReplayEngine(const PayoffReplayEngine&) = delete;
    PayoffReplayEngine& operator=(const PayoffReplayEngine&) = delete;
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Set the strategy to analyze
     */
    void set_strategy(const engine::Strategy& strategy);
    
    /**
     * @brief Set implied volatility for pricing (or use surface)
     */
    void set_implied_volatility(double iv);
    
    /**
     * @brief Set replay configuration
     */
    void set_config(const ReplayConfig& config);
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    void on_tick(ReplayTickCallback callback);
    void on_payoff(ReplayPayoffCallback callback);
    void on_progress(ReplayProgressCallback callback);
    void on_complete(ReplayCompleteCallback callback);
    
    // ========================================================================
    // Replay Control
    // ========================================================================
    
    /**
     * @brief Start replay
     * @return True if replay started successfully
     */
    bool start();
    
    /**
     * @brief Stop replay
     */
    void stop();
    
    /**
     * @brief Check if replay is running
     */
    [[nodiscard]] bool is_running() const noexcept;
    
    /**
     * @brief Get current replay position
     */
    [[nodiscard]] Timestamp current_position() const noexcept;
    
    /**
     * @brief Get replay progress (0.0 to 1.0)
     */
    [[nodiscard]] double progress() const noexcept;
    
    // ========================================================================
    // Results
    // ========================================================================
    
    /**
     * @brief Get all payoff snapshots after replay completes
     */
    [[nodiscard]] const std::vector<ReplayPayoffSnapshot>& get_payoff_history() const;
    
    /**
     * @brief Get summary statistics
     */
    struct ReplaySummary {
        Timestamp start_time;
        Timestamp end_time;
        size_t ticks_processed;
        size_t payoff_snapshots;
        
        double initial_underlying_price;
        double final_underlying_price;
        double price_change_pct;
        
        double initial_pnl;
        double final_pnl;
        double max_pnl;
        double min_pnl;
        double pnl_std_dev;
        
        // Greeks evolution
        double avg_delta;
        double avg_gamma;
        double avg_theta;
        double avg_vega;
    };
    
    [[nodiscard]] ReplaySummary get_summary() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Factory
// ============================================================================

/**
 * @brief Create replay engine from config
 */
std::unique_ptr<PayoffReplayEngine> create_replay_engine_from_config();

} // namespace payoff::core
