/**
 * @file replay_service.cpp
 * @brief Strategy replay service implementation
 * 
 * Provides historical replay with no-lookahead simulation for strategy backtesting.
 */

#include "api/replay_service.hpp"
#include "core/datasource.hpp"
#include "core/instrument_manager.hpp"
#include "payoff/calculator.hpp"
#include "payoff/pricing.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <unordered_map>

namespace payoff::replay {

// ============================================================================
// Implementation
// ============================================================================

class ReplayServiceImpl : public ReplayService {
public:
    ReplayServiceImpl() {
        // Initialize data source
        data_source_ = core::create_clickhouse_source_from_config();
    }
    
    // ========================================================================
    // Strategy Replay
    // ========================================================================
    
    StrategyReplayResult replay_strategy(const StrategyReplayRequest& request) override {
        StrategyReplayResult result;
        
        if (request.strategy.legs.empty()) {
            return result;
        }
        
        // Get underlying price data over the time range
        std::vector<core::DepthSnapshot> snapshots;
        
        // Collect instrument IDs for the strategy legs
        std::vector<uint32_t> instrument_ids;
        for (const auto& leg : request.strategy.legs) {
            if (leg.instrument_id && *leg.instrument_id > 0) {
                instrument_ids.push_back(*leg.instrument_id);
            }
        }
        
        // Get underlying index data
        auto& inst_mgr = core::get_instrument_manager();
        
        // Try to find underlying instrument (e.g., NIFTY 50)
        auto underlying_insts = inst_mgr.resolve_by_tradingsymbol(request.strategy.underlying);
        if (!underlying_insts.empty()) {
            uint32_t underlying_ch_id = inst_mgr.get_clickhouse_id(
                underlying_insts[0]->instrument_token);
            instrument_ids.push_back(underlying_ch_id);
        }
        
        // Query historical data
        if (data_source_ && !instrument_ids.empty()) {
            core::TimeRange range{request.start, request.end};
            std::vector<std::string> symbols;
            for (uint32_t id : instrument_ids) {
                symbols.push_back(std::to_string(id));
            }
            
            data_source_->replay_snapshots(symbols, range, 
                [&](const core::DepthSnapshot& snap) {
                    snapshots.push_back(snap);
                    return true;
                });
        }
        
        // Process snapshots at requested intervals
        engine::PayoffCalculator calculator;
        
        int64_t current_ts = request.start.count();
        int64_t end_ts = request.end.count();
        
        double running_sum = 0.0;
        double running_sum_sq = 0.0;
        int count = 0;
        
        while (current_ts <= end_ts) {
            StrategySnapshot snap;
            snap.timestamp = core::Timestamp(current_ts);
            
            // Find closest data point
            double underlying_price = request.strategy.underlying_price;
            for (const auto& data_snap : snapshots) {
                if (data_snap.exchange_timestamp.count() <= current_ts) {
                    if (data_snap.trade.last_price > 0) {
                        underlying_price = data_snap.trade.last_price;
                    }
                }
            }
            
            snap.underlying_price = underlying_price;
            
            // Calculate P&L at this point
            engine::Strategy strat_copy = request.strategy;
            strat_copy.underlying_price = underlying_price;
            
            auto curve = calculator.calculate_expiry_payoff(strat_copy);
            
            // Find P&L at current underlying price
            snap.total_pnl = 0.0;
            for (const auto& pt : curve.points) {
                if (std::abs(pt.spot - underlying_price) < 1.0) {
                    snap.total_pnl = pt.pnl;
                    break;
                }
            }
            
            // Calculate Greeks if requested
            if (request.include_greeks) {
                // Calculate days to expiry (estimate)
                int dte = 26;  // Default
                
                auto greeks = calculator.calculate_strategy_greeks(strat_copy, 0.15, dte);
                snap.delta = greeks.delta;
                snap.gamma = greeks.gamma;
                snap.theta = greeks.theta;
                snap.vega = greeks.vega;
            }
            
            result.snapshots.push_back(snap);
            
            // Update statistics
            running_sum += snap.total_pnl;
            running_sum_sq += snap.total_pnl * snap.total_pnl;
            ++count;
            
            if (result.snapshots.size() == 1) {
                result.initial_pnl = snap.total_pnl;
                result.max_pnl = snap.total_pnl;
                result.min_pnl = snap.total_pnl;
            } else {
                result.max_pnl = std::max(result.max_pnl, snap.total_pnl);
                result.min_pnl = std::min(result.min_pnl, snap.total_pnl);
            }
            result.final_pnl = snap.total_pnl;
            
            current_ts += request.interval_ms;
        }
        
        // Calculate standard deviation
        if (count > 1) {
            double mean = running_sum / count;
            double variance = (running_sum_sq / count) - (mean * mean);
            result.pnl_std_dev = std::sqrt(std::max(0.0, variance));
        }
        
        return result;
    }
    
    // ========================================================================
    // Prediction at Historical Time
    // ========================================================================
    
    PredictionResult get_prediction(const PredictionRequest& request) override {
        PredictionResult result;
        result.as_of_timestamp = request.as_of_timestamp;
        
        engine::PayoffCalculator calculator;
        
        // Get market state at the timestamp
        result.market_state.underlying_price = request.strategy.underlying_price;
        result.market_state.atm_iv = 0.15;  // Default, would query actual IV
        result.market_state.days_to_expiry = 26;  // Would calculate from expiry
        
        // Generate predictions for each horizon
        for (const auto& horizon : request.horizons) {
            PredictedPayoff pred;
            pred.horizon = horizon;
            
            // Create strategy copy with adjustments
            engine::Strategy adjusted = request.strategy;
            
            // Apply IV shift if specified
            double iv = result.market_state.atm_iv;
            if (horizon.iv_shift_pct) {
                iv *= (1.0 + *horizon.iv_shift_pct / 100.0);
            }
            
            // Apply spot shift if specified
            double spot = adjusted.underlying_price;
            if (horizon.spot_shift_pct) {
                spot *= (1.0 + *horizon.spot_shift_pct / 100.0);
            }
            adjusted.underlying_price = spot;
            
            // Reduce DTE by horizon days
            int adjusted_dte = std::max(1, result.market_state.days_to_expiry - horizon.days_forward);
            
            // Calculate payoff curve
            auto curve = calculator.calculate_expiry_payoff(adjusted);
            pred.payoff_curve = curve.points;
            
            // Calculate Greeks
            pred.predicted_greeks = calculator.calculate_strategy_greeks(adjusted, iv, adjusted_dte);
            
            result.predictions.push_back(pred);
        }
        
        return result;
    }
    
    // ========================================================================
    // Prediction vs Reality Comparison
    // ========================================================================
    
    CompareResult compare_prediction_vs_actual(const CompareRequest& request) override {
        CompareResult result;
        result.prediction_timestamp = request.prediction_timestamp;
        result.actual_timestamp = request.actual_timestamp;
        
        // Calculate time elapsed
        int64_t elapsed_ms = request.actual_timestamp.count() - request.prediction_timestamp.count();
        result.time_elapsed_hours = elapsed_ms / (1000 * 60 * 60);
        
        engine::PayoffCalculator calculator;
        
        // Get state at prediction time (this is what we predicted)
        result.at_prediction.underlying_price = request.strategy.underlying_price;
        auto pred_curve = calculator.calculate_expiry_payoff(request.strategy);
        
        // Find P&L at current spot (at prediction time)
        for (const auto& pt : pred_curve.points) {
            if (std::abs(pt.spot - result.at_prediction.underlying_price) < 1.0) {
                result.at_prediction.predicted_pnl = pt.pnl;
                break;
            }
        }
        
        // Get breakeven from prediction
        if (!pred_curve.breakevens.empty()) {
            result.at_prediction.predicted_breakeven = pred_curve.breakevens[0];
        }
        
        // Simulate what actually happened (would query historical data)
        // For now, simulate a spot movement
        double spot_change_pct = 0.65;  // 0.65% move
        result.at_actual.underlying_price = result.at_prediction.underlying_price * 
            (1.0 + spot_change_pct / 100.0);
        
        // Recalculate with actual underlying price
        engine::Strategy actual_strat = request.strategy;
        actual_strat.underlying_price = result.at_actual.underlying_price;
        auto actual_curve = calculator.calculate_expiry_payoff(actual_strat);
        
        for (const auto& pt : actual_curve.points) {
            if (std::abs(pt.spot - result.at_actual.underlying_price) < 1.0) {
                result.at_actual.actual_pnl = pt.pnl;
                break;
            }
        }
        
        if (!actual_curve.breakevens.empty()) {
            result.at_actual.actual_breakeven = actual_curve.breakevens[0];
        }
        
        // Calculate deviation metrics
        result.deviation.pnl_deviation = result.at_actual.actual_pnl - result.at_prediction.predicted_pnl;
        if (std::abs(result.at_prediction.predicted_pnl) > 0.01) {
            result.deviation.pnl_deviation_pct = 
                (result.deviation.pnl_deviation / std::abs(result.at_prediction.predicted_pnl)) * 100.0;
        }
        
        result.deviation.breakeven_shift = result.at_actual.actual_breakeven - 
            result.at_prediction.predicted_breakeven;
        
        // Estimate IV change (would be from actual data)
        result.deviation.iv_change = -0.018;  // Simulated -1.8% IV drop
        
        // Delta drift
        result.deviation.delta_drift = 0.08;  // Simulated
        
        // Calculate accuracy score (simple formula)
        double pnl_accuracy = 1.0 - std::min(1.0, std::abs(result.deviation.pnl_deviation_pct) / 100.0);
        double be_accuracy = 1.0 - std::min(1.0, std::abs(result.deviation.breakeven_shift) / 
            result.at_prediction.underlying_price * 100.0);
        result.deviation.prediction_accuracy_score = (pnl_accuracy + be_accuracy) / 2.0;
        
        // Generate insights
        if (result.deviation.pnl_deviation > 0) {
            result.insights.push_back("Actual profit exceeded prediction by ₹" + 
                std::to_string(static_cast<int>(result.deviation.pnl_deviation)) +
                " (+" + std::to_string(static_cast<int>(result.deviation.pnl_deviation_pct)) + "%)");
        } else if (result.deviation.pnl_deviation < 0) {
            result.insights.push_back("Actual profit fell short of prediction by ₹" +
                std::to_string(static_cast<int>(-result.deviation.pnl_deviation)));
        }
        
        if (std::abs(result.deviation.iv_change) > 0.01) {
            result.insights.push_back("IV changed " + 
                std::to_string(static_cast<int>(result.deviation.iv_change * 100)) +
                "%, affecting Vega P&L contribution");
        }
        
        double spot_move = result.at_actual.underlying_price - result.at_prediction.underlying_price;
        if (std::abs(spot_move) > 10) {
            std::string sign = (spot_move > 0 ? "+" : "");
            std::string delta_msg = (spot_move > 0 ? "dominated" : "reduced");
            result.insights.push_back(std::string("Spot moved ") + sign + 
                std::to_string(static_cast<int>(spot_move)) + ", Delta gains " + delta_msg);
        }
        
        return result;
    }
    
    // ========================================================================
    // Session Management
    // ========================================================================
    
    std::string create_session(const engine::Strategy& strategy,
                               core::Timestamp start,
                               core::Timestamp end,
                               double speed,
                               int64_t payoff_interval_ms) override {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        
        // Generate session ID
        std::string session_id = "replay-" + std::to_string(next_session_id_++);
        
        ReplaySession session;
        session.session_id = session_id;
        session.strategy = strategy;
        session.start = start;
        session.end = end;
        session.current = start;
        session.speed = speed;
        session.payoff_interval_ms = payoff_interval_ms;
        session.progress_pct = 0.0;
        session.has_more = true;
        
        sessions_[session_id] = session;
        
        return session_id;
    }
    
    std::optional<ReplaySession> get_session(const std::string& session_id) override {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return std::nullopt;
        }
        return it->second;
    }
    
    std::optional<StrategySnapshot> step_session(const std::string& session_id) override {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return std::nullopt;
        }
        
        auto& session = it->second;
        
        if (!session.has_more) {
            return std::nullopt;
        }
        
        // Advance current timestamp
        int64_t step_ms = static_cast<int64_t>(session.payoff_interval_ms * session.speed);
        session.current = core::Timestamp(session.current.count() + step_ms);
        
        // Check if we've reached the end
        if (session.current.count() >= session.end.count()) {
            session.current = session.end;
            session.has_more = false;
        }
        
        // Calculate progress
        int64_t total_range = session.end.count() - session.start.count();
        int64_t elapsed = session.current.count() - session.start.count();
        session.progress_pct = total_range > 0 ? 
            (static_cast<double>(elapsed) / total_range) * 100.0 : 100.0;
        
        // Calculate snapshot at current time
        StrategySnapshot snap;
        snap.timestamp = session.current;
        snap.underlying_price = session.strategy.underlying_price;  // Would update from data
        
        engine::PayoffCalculator calculator;
        auto greeks = calculator.calculate_strategy_greeks(session.strategy, 0.15, 26);
        snap.delta = greeks.delta;
        snap.gamma = greeks.gamma;
        snap.theta = greeks.theta;
        snap.vega = greeks.vega;
        
        auto curve = calculator.calculate_expiry_payoff(session.strategy);
        for (const auto& pt : curve.points) {
            if (std::abs(pt.spot - snap.underlying_price) < 1.0) {
                snap.total_pnl = pt.pnl;
                break;
            }
        }
        
        session.current_snapshot = snap;
        
        return snap;
    }
    
    bool seek_session(const std::string& session_id, core::Timestamp target) override {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return false;
        }
        
        auto& session = it->second;
        
        // Clamp target to valid range
        int64_t target_ts = std::clamp(target.count(), 
            session.start.count(), session.end.count());
        session.current = core::Timestamp(target_ts);
        
        // Update progress
        int64_t total_range = session.end.count() - session.start.count();
        int64_t elapsed = session.current.count() - session.start.count();
        session.progress_pct = total_range > 0 ? 
            (static_cast<double>(elapsed) / total_range) * 100.0 : 100.0;
        
        session.has_more = (session.current.count() < session.end.count());
        
        return true;
    }
    
    bool delete_session(const std::string& session_id) override {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        return sessions_.erase(session_id) > 0;
    }
    
    // ========================================================================
    // Market Events
    // ========================================================================
    
    std::vector<MarketEvent> get_events(const std::string& underlying,
                                         core::Timestamp start,
                                         core::Timestamp end) override {
        std::vector<MarketEvent> events;
        
        // In production, would query ClickHouse for significant events
        // For now, return simulated events
        
        int64_t range_ms = end.count() - start.count();
        
        // Simulate an IV spike event
        MarketEvent iv_spike;
        iv_spike.timestamp = core::Timestamp(start.count() + range_ms / 4);
        iv_spike.type = "IV_SPIKE";
        iv_spike.description = underlying + " ATM IV jumped 3% in 5 minutes";
        iv_spike.magnitude = 0.03;
        events.push_back(iv_spike);
        
        // Simulate a price gap
        MarketEvent gap;
        gap.timestamp = core::Timestamp(start.count() + range_ms / 2);
        gap.type = "PRICE_GAP";
        gap.description = underlying + " gapped down 0.8% at open";
        gap.magnitude = -0.008;
        events.push_back(gap);
        
        // Simulate OI buildup
        MarketEvent oi;
        oi.timestamp = core::Timestamp(start.count() + 3 * range_ms / 4);
        oi.type = "OI_BUILDUP";
        oi.description = "26300 CE saw 500K OI addition";
        oi.strike = 26300.0;
        oi.option_type = "CE";
        events.push_back(oi);
        
        return events;
    }
    
    // ========================================================================
    // Batch Historical Payoff
    // ========================================================================
    
    BatchPayoffResult batch_historical_payoff(const BatchPayoffRequest& request) override {
        BatchPayoffResult result;
        
        engine::PayoffCalculator calculator;
        
        for (const auto& ts : request.timestamps) {
            BatchPayoffResult::TimestampResult tr;
            tr.timestamp = ts;
            
            // Would fetch actual underlying price at this timestamp
            // For now, use strategy's underlying price
            engine::Strategy strat = request.strategy;
            
            auto curve = calculator.calculate_expiry_payoff(strat);
            
            // Find P&L at underlying price
            for (const auto& pt : curve.points) {
                if (std::abs(pt.spot - strat.underlying_price) < 1.0) {
                    tr.pnl = pt.pnl;
                    break;
                }
            }
            
            if (request.include_greeks) {
                tr.greeks = calculator.calculate_strategy_greeks(strat, 0.15, 26);
            }
            
            result.results.push_back(tr);
        }
        
        return result;
    }
    
private:
    std::unique_ptr<core::MarketDataSource> data_source_;
    
    std::mutex sessions_mutex_;
    std::unordered_map<std::string, ReplaySession> sessions_;
    uint64_t next_session_id_ = 1;
};

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<ReplayService> create_replay_service() {
    return std::make_unique<ReplayServiceImpl>();
}

} // namespace payoff::replay