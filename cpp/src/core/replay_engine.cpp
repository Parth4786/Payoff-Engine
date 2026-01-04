/**
 * @file replay_engine.cpp
 * @brief Implementation of PayoffReplayEngine
 * 
 * Replays historical market data from ClickHouse and calculates
 * payoff at each point in time for strategy verification.
 */

#include "core/replay_engine.hpp"
#include "core/config.hpp"
#include "payoff/pricing.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <numeric>
#include <thread>

namespace payoff::core {

// ============================================================================
// Implementation
// ============================================================================

struct PayoffReplayEngine::Impl {
    std::shared_ptr<MarketDataSource> data_source;
    std::shared_ptr<InstrumentManager> instrument_manager;
    
    // Configuration
    ReplayConfig config;
    engine::Strategy strategy;
    double implied_volatility = 0.20;
    engine::PayoffCalculator calculator;
    
    // State
    std::atomic<bool> running{false};
    std::atomic<Timestamp> current_timestamp{Timestamp(0)};
    
    // Results
    std::vector<ReplayPayoffSnapshot> payoff_history;
    std::mutex results_mutex;
    
    // Callbacks
    ReplayTickCallback tick_callback;
    ReplayPayoffCallback payoff_callback;
    ReplayProgressCallback progress_callback;
    ReplayCompleteCallback complete_callback;
    
    // Statistics
    size_t ticks_processed = 0;
    double initial_price = 0.0;
    double final_price = 0.0;
    
    // Replay thread
    std::unique_ptr<std::thread> replay_thread;
    
    // ========================================================================
    // Core Replay Logic
    // ========================================================================
    
    void run_replay() {
        ticks_processed = 0;
        payoff_history.clear();
        
        Timestamp last_payoff_time(0);
        int payoff_interval_ms = config.payoff_sample_interval_ms;
        
        auto total_duration = config.time_range.end - config.time_range.start;
        
        // Replay callback
        auto on_snapshot = [&](const DepthSnapshot& snap) {
            if (!running) return;
            
            ticks_processed++;
            current_timestamp = snap.exchange_timestamp;
            
            // Update underlying price
            double price = snap.trade.last_price;
            if (ticks_processed == 1) {
                initial_price = price;
            }
            final_price = price;
            
            // Convert to ReplayTick
            ReplayTick tick;
            tick.timestamp = snap.exchange_timestamp;
            tick.instrument_token = snap.instrument_id;
            tick.symbol = snap.symbol;
            tick.last_price = price;
            tick.volume = snap.trade.total_traded_quantity;
            tick.oi = snap.trade.open_interest;
            
            if (!snap.bids.empty()) {
                tick.bid_price = snap.bids[0].price;
                tick.bids = snap.bids;
            }
            if (!snap.asks.empty()) {
                tick.ask_price = snap.asks[0].price;
                tick.asks = snap.asks;
            }
            
            // Invoke tick callback
            if (tick_callback) {
                tick_callback(tick);
            }
            
            // Calculate payoff at intervals
            auto elapsed = snap.exchange_timestamp - last_payoff_time;
            bool should_calculate_payoff = 
                config.emit_payoff_at_each_tick ||
                elapsed.count() >= payoff_interval_ms;
            
            if (should_calculate_payoff && strategy.underlying_price > 0) {
                // Update strategy with current price
                engine::Strategy current_strategy = strategy;
                current_strategy.underlying_price = price;
                
                // Calculate payoff
                ReplayPayoffSnapshot payoff_snap = calculate_payoff_snapshot(
                    snap.exchange_timestamp, price, current_strategy);
                
                {
                    std::lock_guard<std::mutex> lock(results_mutex);
                    payoff_history.push_back(payoff_snap);
                }
                
                if (payoff_callback) {
                    payoff_callback(payoff_snap);
                }
                
                last_payoff_time = snap.exchange_timestamp;
            }
            
            // Progress callback
            if (progress_callback && total_duration.count() > 0) {
                auto progress_duration = snap.exchange_timestamp - config.time_range.start;
                double progress = static_cast<double>(progress_duration.count()) / 
                                 static_cast<double>(total_duration.count());
                progress_callback(progress * 100.0, snap.exchange_timestamp);
            }
            
            // Speed control
            if (config.speed_multiplier > 0) {
                // Simulate real-time playback
                std::this_thread::sleep_for(
                    std::chrono::microseconds(
                        static_cast<int64_t>(1000.0 / config.speed_multiplier)));
            }
        };
        
        // Execute replay
        try {
            data_source->replay_snapshots(
                config.symbols,
                config.time_range,
                on_snapshot);
        } catch (const std::exception& e) {
            // Log error but continue
        }
        
        running = false;
        
        // Complete callback
        if (complete_callback) {
            complete_callback(ticks_processed, payoff_history.size());
        }
    }
    
    ReplayPayoffSnapshot calculate_payoff_snapshot(
        Timestamp ts,
        double underlying_price,
        const engine::Strategy& current_strategy) {
        
        ReplayPayoffSnapshot snap;
        snap.timestamp = ts;
        snap.underlying_price = underlying_price;
        
        // Calculate P&L for each leg
        double total_pnl = 0.0;
        snap.leg_pnls.reserve(current_strategy.legs.size());
        
        for (const auto& leg : current_strategy.legs) {
            // Estimate time to expiry (simplified - would need actual expiry date)
            double days_to_expiry = 30.0;  // Default 30 DTE
            double time_to_expiry = std::max(days_to_expiry / 365.0, 0.001);
            
            // Calculate current option price
            engine::PricingParams params;
            params.spot = underlying_price;
            params.strike = leg.strike;
            params.time_to_expiry = time_to_expiry;
            params.volatility = implied_volatility;
            params.risk_free_rate = 0.07;
            
            double current_price = engine::bs_price(params, leg.type);
            
            // P&L = (current_price - entry_price) * quantity * direction
            double multiplier = (leg.side == engine::Side::Buy) ? 1.0 : -1.0;
            double leg_pnl = multiplier * (current_price - leg.premium) * leg.total_quantity();
            
            snap.leg_pnls.push_back(leg_pnl);
            total_pnl += leg_pnl;
            
            // Accumulate Greeks
            engine::Greeks leg_greeks = engine::calculate_greeks(params, leg.type);
            snap.portfolio_greeks.delta += multiplier * leg_greeks.delta * leg.total_quantity();
            snap.portfolio_greeks.gamma += multiplier * leg_greeks.gamma * leg.total_quantity();
            snap.portfolio_greeks.theta += multiplier * leg_greeks.theta * leg.total_quantity();
            snap.portfolio_greeks.vega += multiplier * leg_greeks.vega * leg.total_quantity();
            snap.portfolio_greeks.rho += multiplier * leg_greeks.rho * leg.total_quantity();
        }
        
        snap.total_pnl = total_pnl;
        snap.unrealized_pnl = total_pnl;  // All unrealized for now
        snap.realized_pnl = 0.0;
        
        // Calculate remaining max profit/loss
        auto curve = calculator.calculate_expiry_payoff(current_strategy);
        snap.max_profit_remaining = curve.max_profit - total_pnl;
        snap.max_loss_remaining = total_pnl - curve.max_loss;
        
        // Estimate probability of profit using delta
        snap.probability_of_profit = 0.5 + snap.portfolio_greeks.delta * 0.3;
        snap.probability_of_profit = std::max(0.0, std::min(1.0, snap.probability_of_profit));
        
        return snap;
    }
};

// ============================================================================
// Public Interface
// ============================================================================

PayoffReplayEngine::PayoffReplayEngine(
    std::shared_ptr<MarketDataSource> data_source,
    std::shared_ptr<InstrumentManager> instrument_manager)
    : impl_(std::make_unique<Impl>()) {
    
    impl_->data_source = std::move(data_source);
    impl_->instrument_manager = std::move(instrument_manager);
}

PayoffReplayEngine::~PayoffReplayEngine() {
    stop();
}

void PayoffReplayEngine::set_strategy(const engine::Strategy& strategy) {
    impl_->strategy = strategy;
}

void PayoffReplayEngine::set_implied_volatility(double iv) {
    impl_->implied_volatility = iv;
}

void PayoffReplayEngine::set_config(const ReplayConfig& config) {
    impl_->config = config;
}

void PayoffReplayEngine::on_tick(ReplayTickCallback callback) {
    impl_->tick_callback = std::move(callback);
}

void PayoffReplayEngine::on_payoff(ReplayPayoffCallback callback) {
    impl_->payoff_callback = std::move(callback);
}

void PayoffReplayEngine::on_progress(ReplayProgressCallback callback) {
    impl_->progress_callback = std::move(callback);
}

void PayoffReplayEngine::on_complete(ReplayCompleteCallback callback) {
    impl_->complete_callback = std::move(callback);
}

bool PayoffReplayEngine::start() {
    if (impl_->running) {
        return false;
    }
    
    if (!impl_->data_source || !impl_->data_source->is_connected()) {
        return false;
    }
    
    impl_->running = true;
    impl_->replay_thread = std::make_unique<std::thread>(&Impl::run_replay, impl_.get());
    
    return true;
}

void PayoffReplayEngine::stop() {
    impl_->running = false;
    
    if (impl_->replay_thread && impl_->replay_thread->joinable()) {
        impl_->replay_thread->join();
    }
}

bool PayoffReplayEngine::is_running() const noexcept {
    return impl_->running;
}

Timestamp PayoffReplayEngine::current_position() const noexcept {
    return impl_->current_timestamp.load();
}

double PayoffReplayEngine::progress() const noexcept {
    auto total = impl_->config.time_range.end - impl_->config.time_range.start;
    auto current = impl_->current_timestamp.load() - impl_->config.time_range.start;
    
    if (total.count() == 0) return 0.0;
    return static_cast<double>(current.count()) / static_cast<double>(total.count());
}

const std::vector<ReplayPayoffSnapshot>& PayoffReplayEngine::get_payoff_history() const {
    return impl_->payoff_history;
}

PayoffReplayEngine::ReplaySummary PayoffReplayEngine::get_summary() const {
    ReplaySummary summary = {};
    
    summary.start_time = impl_->config.time_range.start;
    summary.end_time = impl_->config.time_range.end;
    summary.ticks_processed = impl_->ticks_processed;
    summary.payoff_snapshots = impl_->payoff_history.size();
    
    summary.initial_underlying_price = impl_->initial_price;
    summary.final_underlying_price = impl_->final_price;
    
    if (impl_->initial_price > 0) {
        summary.price_change_pct = 
            (impl_->final_price - impl_->initial_price) / impl_->initial_price * 100.0;
    }
    
    // Analyze payoff history
    if (!impl_->payoff_history.empty()) {
        summary.initial_pnl = impl_->payoff_history.front().total_pnl;
        summary.final_pnl = impl_->payoff_history.back().total_pnl;
        
        double sum_pnl = 0.0;
        double sum_delta = 0.0;
        double sum_gamma = 0.0;
        double sum_theta = 0.0;
        double sum_vega = 0.0;
        
        summary.max_pnl = std::numeric_limits<double>::lowest();
        summary.min_pnl = std::numeric_limits<double>::max();
        
        for (const auto& snap : impl_->payoff_history) {
            sum_pnl += snap.total_pnl;
            sum_delta += snap.portfolio_greeks.delta;
            sum_gamma += snap.portfolio_greeks.gamma;
            sum_theta += snap.portfolio_greeks.theta;
            sum_vega += snap.portfolio_greeks.vega;
            
            summary.max_pnl = std::max(summary.max_pnl, snap.total_pnl);
            summary.min_pnl = std::min(summary.min_pnl, snap.total_pnl);
        }
        
        size_t n = impl_->payoff_history.size();
        double mean_pnl = sum_pnl / n;
        
        // Calculate std dev
        double sq_diff_sum = 0.0;
        for (const auto& snap : impl_->payoff_history) {
            double diff = snap.total_pnl - mean_pnl;
            sq_diff_sum += diff * diff;
        }
        summary.pnl_std_dev = std::sqrt(sq_diff_sum / n);
        
        summary.avg_delta = sum_delta / n;
        summary.avg_gamma = sum_gamma / n;
        summary.avg_theta = sum_theta / n;
        summary.avg_vega = sum_vega / n;
    }
    
    return summary;
}

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<PayoffReplayEngine> create_replay_engine_from_config() {
    auto& cfg = config::config();
    if (!cfg.is_loaded()) {
        cfg.load();
    }
    
    // Create ClickHouse data source
    auto data_source = create_clickhouse_source(
        cfg.ch_host(),
        static_cast<uint16_t>(cfg.ch_port()),
        cfg.ch_database());
    
    // Create instrument manager
    auto instrument_manager = std::make_shared<InstrumentManager>();
    
    // Try to load instruments from configured directory
    std::string inst_dir = cfg.get("KITE_INSTRUMENT_MASTER_DIR", "");
    if (!inst_dir.empty()) {
        try {
            instrument_manager->load_directory(inst_dir);
        } catch (...) {
            // Continue without instruments
        }
    }
    
    return std::make_unique<PayoffReplayEngine>(
        std::shared_ptr<MarketDataSource>(data_source.release()),
        instrument_manager);
}

} // namespace payoff::core
