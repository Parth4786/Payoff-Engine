#pragma once
/**
 * @file replay_service.hpp
 * @brief Strategy replay service for historical backtesting with no-lookahead
 * 
 * Provides endpoints for:
 * - POST /api/replay/strategy     - Run strategy through historical data
 * - POST /api/replay/prediction   - Get predicted payoff at historical time T
 * - POST /api/replay/compare      - Compare prediction vs reality
 * - POST /api/replay/session/*    - Replay session management
 * - GET  /api/replay/events       - Get market events in time range
 * - POST /api/payoff/historical-batch - Batch historical payoff calculation
 */

#include "core/models.hpp"
#include "payoff/models.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace payoff::replay {

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Snapshot of strategy state at a point in time
 */
struct StrategySnapshot {
    core::Timestamp timestamp;
    double underlying_price = 0.0;
    double total_pnl = 0.0;
    
    // Aggregate Greeks
    double delta = 0.0;
    double gamma = 0.0;
    double theta = 0.0;
    double vega = 0.0;
    
    // Per-leg prices
    struct LegPrice {
        double strike = 0.0;
        double price = 0.0;
        double iv = 0.0;
    };
    std::vector<LegPrice> leg_prices;
};

/**
 * @brief Strategy replay request
 */
struct StrategyReplayRequest {
    engine::Strategy strategy;
    core::Timestamp start;
    core::Timestamp end;
    int64_t interval_ms = 60000;  // Default: 1 minute
    bool include_greeks = true;
};

/**
 * @brief Strategy replay result
 */
struct StrategyReplayResult {
    std::vector<StrategySnapshot> snapshots;
    
    // Summary statistics
    double initial_pnl = 0.0;
    double final_pnl = 0.0;
    double max_pnl = 0.0;
    double min_pnl = 0.0;
    double pnl_std_dev = 0.0;
};

/**
 * @brief Prediction horizon configuration
 */
struct PredictionHorizon {
    int days_forward = 1;
    std::optional<double> iv_shift_pct;  // e.g., -2.0 for -2% IV
    std::optional<double> spot_shift_pct;
};

/**
 * @brief Prediction request at historical time T
 */
struct PredictionRequest {
    engine::Strategy strategy;
    core::Timestamp as_of_timestamp;
    std::vector<PredictionHorizon> horizons;
};

/**
 * @brief Market state at a point in time
 */
struct MarketStateAtTime {
    double underlying_price = 0.0;
    double atm_iv = 0.0;
    int days_to_expiry = 0;
};

/**
 * @brief Predicted payoff curve for a horizon
 */
struct PredictedPayoff {
    PredictionHorizon horizon;
    std::vector<engine::PayoffPoint> payoff_curve;
    engine::Greeks predicted_greeks;
};

/**
 * @brief Prediction result
 */
struct PredictionResult {
    core::Timestamp as_of_timestamp;
    MarketStateAtTime market_state;
    std::vector<PredictedPayoff> predictions;
};

/**
 * @brief Compare request: prediction vs reality
 */
struct CompareRequest {
    engine::Strategy strategy;
    core::Timestamp prediction_timestamp;
    core::Timestamp actual_timestamp;
    std::vector<double> comparison_spots;  // Spot prices to compare at
};

/**
 * @brief Deviation metrics between prediction and actual
 */
struct DeviationMetrics {
    double pnl_deviation = 0.0;
    double pnl_deviation_pct = 0.0;
    double breakeven_shift = 0.0;
    double iv_change = 0.0;
    double delta_drift = 0.0;
    double prediction_accuracy_score = 0.0;  // 0-1 score
};

/**
 * @brief Compare result
 */
struct CompareResult {
    core::Timestamp prediction_timestamp;
    core::Timestamp actual_timestamp;
    int64_t time_elapsed_hours = 0;
    
    // State at prediction time
    struct AtPrediction {
        double underlying_price = 0.0;
        double predicted_pnl = 0.0;
        double predicted_breakeven = 0.0;
    } at_prediction;
    
    // State at actual time
    struct AtActual {
        double underlying_price = 0.0;
        double actual_pnl = 0.0;
        double actual_breakeven = 0.0;
    } at_actual;
    
    DeviationMetrics deviation;
    std::vector<std::string> insights;
};

/**
 * @brief Replay session for interactive stepping
 */
struct ReplaySession {
    std::string session_id;
    engine::Strategy strategy;
    core::Timestamp start;
    core::Timestamp end;
    core::Timestamp current;
    double speed = 1.0;
    int64_t payoff_interval_ms = 60000;
    bool auto_calculate_payoff = true;
    
    // Current state
    double progress_pct = 0.0;
    StrategySnapshot current_snapshot;
    bool has_more = true;
};

/**
 * @brief Market event marker
 */
struct MarketEvent {
    core::Timestamp timestamp;
    std::string type;  // IV_SPIKE, PRICE_GAP, OI_BUILDUP, VOLUME_SURGE
    std::string description;
    double magnitude = 0.0;
    std::optional<double> strike;
    std::optional<std::string> option_type;
};

/**
 * @brief Batch historical payoff request
 */
struct BatchPayoffRequest {
    engine::Strategy strategy;
    std::vector<core::Timestamp> timestamps;
    bool include_greeks = true;
};

/**
 * @brief Batch historical payoff result
 */
struct BatchPayoffResult {
    struct TimestampResult {
        core::Timestamp timestamp;
        double pnl = 0.0;
        engine::Greeks greeks;
    };
    std::vector<TimestampResult> results;
};

// ============================================================================
// Replay Service Interface
// ============================================================================

class ReplayService {
public:
    virtual ~ReplayService() = default;
    
    // Core replay operations
    virtual StrategyReplayResult replay_strategy(const StrategyReplayRequest& request) = 0;
    virtual PredictionResult get_prediction(const PredictionRequest& request) = 0;
    virtual CompareResult compare_prediction_vs_actual(const CompareRequest& request) = 0;
    
    // Session management
    virtual std::string create_session(const engine::Strategy& strategy,
                                       core::Timestamp start,
                                       core::Timestamp end,
                                       double speed = 1.0,
                                       int64_t payoff_interval_ms = 60000) = 0;
    virtual std::optional<ReplaySession> get_session(const std::string& session_id) = 0;
    virtual std::optional<StrategySnapshot> step_session(const std::string& session_id) = 0;
    virtual bool seek_session(const std::string& session_id, core::Timestamp target) = 0;
    virtual bool delete_session(const std::string& session_id) = 0;
    
    // Event markers
    virtual std::vector<MarketEvent> get_events(const std::string& underlying,
                                                 core::Timestamp start,
                                                 core::Timestamp end) = 0;
    
    // Batch operations
    virtual BatchPayoffResult batch_historical_payoff(const BatchPayoffRequest& request) = 0;
};

// Factory
std::unique_ptr<ReplayService> create_replay_service();

} // namespace payoff::replay