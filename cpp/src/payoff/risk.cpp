/**
 * @file risk.cpp
 * @brief Risk calculations - VaR, max drawdown, position limits, kill switch
 * 
 * Implements comprehensive risk metrics for option positions.
 */

#include "payoff/calculator.hpp"
#include "payoff/pricing.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

namespace payoff::engine {

// ============================================================================
// Value at Risk (VaR) Calculator
// ============================================================================

struct VaRResult {
    double var_95;           // 95% VaR (1-day)
    double var_99;           // 99% VaR (1-day)
    double cvar_95;          // Conditional VaR (Expected Shortfall)
    double max_loss;         // Maximum loss in simulation
    int num_simulations;
};

class VaRCalculator {
public:
    struct Config {
        int num_simulations = 10000;
        double spot_vol = 0.02;      // Daily spot volatility (2%)
        double iv_vol = 0.10;        // Daily IV volatility (10% relative)
        double correlation = -0.3;   // Spot-IV correlation (negative = fear)
        int holding_period = 1;      // Days
    };
    
    explicit VaRCalculator(Config config = {}) : config_(std::move(config)) {}
    
    /**
     * @brief Calculate VaR for a strategy
     */
    VaRResult calculate(
        PayoffCalculator& calc,
        const Strategy& strategy,
        double current_iv) const {
        
        std::vector<double> pnls;
        pnls.reserve(config_.num_simulations);
        
        // Monte Carlo simulation
        std::mt19937 gen(42);  // Fixed seed for reproducibility
        std::normal_distribution<> norm(0.0, 1.0);
        
        // Calculate base P&L
        double base_pnl = calculate_strategy_pnl(calc, strategy, 
            strategy.underlying_price, current_iv, 0);
        
        for (int i = 0; i < config_.num_simulations; ++i) {
            // Generate correlated shocks
            double z1 = norm(gen);
            double z2 = config_.correlation * z1 + 
                       std::sqrt(1 - config_.correlation * config_.correlation) * norm(gen);
            
            // Apply shocks
            double spot_shock = config_.spot_vol * std::sqrt(config_.holding_period) * z1;
            double iv_shock = config_.iv_vol * z2;
            
            double new_spot = strategy.underlying_price * (1 + spot_shock);
            double new_iv = current_iv * (1 + iv_shock);
            new_iv = std::max(0.05, new_iv);  // Floor IV at 5%
            
            // Calculate P&L
            double sim_pnl = calculate_strategy_pnl(calc, strategy, 
                new_spot, new_iv, config_.holding_period);
            
            pnls.push_back(sim_pnl - base_pnl);
        }
        
        // Sort for percentile calculation
        std::sort(pnls.begin(), pnls.end());
        
        VaRResult result;
        result.num_simulations = config_.num_simulations;
        
        // 95% VaR (5th percentile of losses)
        int idx_95 = static_cast<int>(0.05 * config_.num_simulations);
        result.var_95 = -pnls[idx_95];
        
        // 99% VaR (1st percentile of losses)
        int idx_99 = static_cast<int>(0.01 * config_.num_simulations);
        result.var_99 = -pnls[idx_99];
        
        // Conditional VaR (average of worst 5%)
        double sum_worst = 0.0;
        for (int i = 0; i < idx_95; ++i) {
            sum_worst += pnls[i];
        }
        result.cvar_95 = -sum_worst / idx_95;
        
        // Max loss
        result.max_loss = -pnls.front();
        
        return result;
    }

private:
    Config config_;
    
    double calculate_strategy_pnl(
        PayoffCalculator& calc,
        const Strategy& strategy,
        double spot,
        double iv,
        int days_forward) const {
        
        double total_pnl = 0.0;
        
        for (const auto& leg : strategy.legs) {
            double time_to_expiry = std::max(
                (30 - days_forward) / 365.0, 0.001);  // Simplified DTE
            
            PricingParams params;
            params.spot = spot;
            params.strike = leg.strike;
            params.time_to_expiry = time_to_expiry;
            params.volatility = iv;
            params.risk_free_rate = 0.07;
            
            double option_price = bs_price(params, leg.type);
            double multiplier = (leg.side == Side::Buy) ? 1.0 : -1.0;
            double pnl = multiplier * (option_price - leg.premium) * leg.total_quantity();
            total_pnl += pnl;
        }
        
        return total_pnl;
    }
};

// ============================================================================
// Position Risk Metrics
// ============================================================================

struct PositionRisk {
    double margin_required;
    double margin_used;
    double margin_utilization;   // % of available margin used
    
    double notional_exposure;    // Total notional value
    double delta_exposure;       // Net delta in underlying terms
    double gamma_exposure;
    double vega_exposure;
    
    double max_profit;
    double max_loss;
    double risk_reward_ratio;
    
    double probability_of_profit;  // Based on current Greeks
    double expected_value;
};

/**
 * @brief Calculate comprehensive position risk metrics
 */
PositionRisk calculate_position_risk(
    const Strategy& strategy,
    double current_iv,
    double available_margin) {
    
    PositionRisk risk = {};
    
    // Calculate aggregate Greeks
    Greeks total_greeks = {};
    double notional = 0.0;
    
    for (const auto& leg : strategy.legs) {
        double time_to_expiry = 30.0 / 365.0;  // Assume 30 DTE
        
        PricingParams params;
        params.spot = strategy.underlying_price;
        params.strike = leg.strike;
        params.time_to_expiry = time_to_expiry;
        params.volatility = current_iv;
        params.risk_free_rate = 0.07;
        
        Greeks leg_greeks = calculate_greeks(params, leg.type);
        
        double multiplier = (leg.side == Side::Buy) ? 1.0 : -1.0;
        double qty = leg.total_quantity();
        
        total_greeks.delta += multiplier * leg_greeks.delta * qty;
        total_greeks.gamma += multiplier * leg_greeks.gamma * qty;
        total_greeks.theta += multiplier * leg_greeks.theta * qty;
        total_greeks.vega += multiplier * leg_greeks.vega * qty;
        
        notional += strategy.underlying_price * qty;
    }
    
    risk.notional_exposure = notional;
    risk.delta_exposure = total_greeks.delta * strategy.underlying_price;
    risk.gamma_exposure = total_greeks.gamma;
    risk.vega_exposure = total_greeks.vega;
    
    // Estimate margin (simplified SPAN-like)
    risk.margin_required = std::abs(risk.delta_exposure) * 0.15 +  // 15% of delta
                          std::abs(risk.gamma_exposure) * strategy.underlying_price * 0.05;
    risk.margin_used = risk.margin_required;
    risk.margin_utilization = available_margin > 0 ? 
        (risk.margin_used / available_margin) * 100.0 : 0.0;
    
    // Calculate max profit/loss from payoff curve
    PayoffCalculator calc;
    auto curve = calc.calculate_expiry_payoff(strategy);
    risk.max_profit = curve.max_profit;
    risk.max_loss = curve.max_loss;
    
    risk.risk_reward_ratio = risk.max_loss != 0 ? 
        std::abs(risk.max_profit / risk.max_loss) : 0.0;
    
    // Probability of profit (simplified using delta as proxy)
    // For single option: PoP ≈ |delta| for ITM, 1-|delta| for OTM
    risk.probability_of_profit = 0.5 + total_greeks.delta * 0.5;
    risk.probability_of_profit = std::max(0.0, std::min(1.0, risk.probability_of_profit));
    
    // Expected value
    risk.expected_value = risk.probability_of_profit * risk.max_profit + 
                         (1 - risk.probability_of_profit) * risk.max_loss;
    
    return risk;
}

// ============================================================================
// Kill Switch - Position Limits and Circuit Breakers
// ============================================================================

struct KillSwitchConfig {
    double max_position_value = 1000000.0;   // Max notional
    double max_margin_utilization = 80.0;    // %
    double max_delta_exposure = 100000.0;    // In underlying terms
    double max_loss_per_trade = 50000.0;
    double max_daily_loss = 100000.0;
    int max_positions = 50;
    double max_vega_exposure = 50000.0;
};

struct KillSwitchStatus {
    bool is_tripped = false;
    std::vector<std::string> violations;
    
    bool check_position_value = false;
    bool check_margin = false;
    bool check_delta = false;
    bool check_loss = false;
    bool check_count = false;
    bool check_vega = false;
};

/**
 * @brief Check if kill switch should be triggered
 */
KillSwitchStatus check_kill_switch(
    const PositionRisk& risk,
    double current_loss,
    double daily_loss,
    int position_count,
    const KillSwitchConfig& config) {
    
    KillSwitchStatus status;
    
    // Check position value
    if (risk.notional_exposure > config.max_position_value) {
        status.is_tripped = true;
        status.check_position_value = true;
        status.violations.push_back(
            "Position value " + std::to_string(risk.notional_exposure) + 
            " exceeds limit " + std::to_string(config.max_position_value));
    }
    
    // Check margin utilization
    if (risk.margin_utilization > config.max_margin_utilization) {
        status.is_tripped = true;
        status.check_margin = true;
        status.violations.push_back(
            "Margin utilization " + std::to_string(risk.margin_utilization) + 
            "% exceeds limit " + std::to_string(config.max_margin_utilization) + "%");
    }
    
    // Check delta exposure
    if (std::abs(risk.delta_exposure) > config.max_delta_exposure) {
        status.is_tripped = true;
        status.check_delta = true;
        status.violations.push_back(
            "Delta exposure " + std::to_string(risk.delta_exposure) + 
            " exceeds limit " + std::to_string(config.max_delta_exposure));
    }
    
    // Check current trade loss
    if (current_loss > config.max_loss_per_trade) {
        status.is_tripped = true;
        status.check_loss = true;
        status.violations.push_back(
            "Trade loss " + std::to_string(current_loss) + 
            " exceeds limit " + std::to_string(config.max_loss_per_trade));
    }
    
    // Check daily loss
    if (daily_loss > config.max_daily_loss) {
        status.is_tripped = true;
        status.check_loss = true;
        status.violations.push_back(
            "Daily loss " + std::to_string(daily_loss) + 
            " exceeds limit " + std::to_string(config.max_daily_loss));
    }
    
    // Check position count
    if (position_count > config.max_positions) {
        status.is_tripped = true;
        status.check_count = true;
        status.violations.push_back(
            "Position count " + std::to_string(position_count) + 
            " exceeds limit " + std::to_string(config.max_positions));
    }
    
    // Check vega exposure
    if (std::abs(risk.vega_exposure) > config.max_vega_exposure) {
        status.is_tripped = true;
        status.check_vega = true;
        status.violations.push_back(
            "Vega exposure " + std::to_string(risk.vega_exposure) + 
            " exceeds limit " + std::to_string(config.max_vega_exposure));
    }
    
    return status;
}

// ============================================================================
// Drawdown Calculator
// ============================================================================

struct DrawdownResult {
    double current_drawdown;
    double max_drawdown;
    double max_drawdown_duration;  // In days
    double recovery_factor;        // Total return / max drawdown
    std::vector<double> drawdown_series;
};

/**
 * @brief Calculate drawdown metrics from P&L series
 */
DrawdownResult calculate_drawdown(const std::vector<double>& pnl_series) {
    DrawdownResult result;
    
    if (pnl_series.empty()) {
        return result;
    }
    
    // Calculate cumulative P&L
    std::vector<double> cumulative(pnl_series.size());
    cumulative[0] = pnl_series[0];
    for (size_t i = 1; i < pnl_series.size(); ++i) {
        cumulative[i] = cumulative[i-1] + pnl_series[i];
    }
    
    // Calculate running max
    std::vector<double> running_max(pnl_series.size());
    running_max[0] = cumulative[0];
    for (size_t i = 1; i < cumulative.size(); ++i) {
        running_max[i] = std::max(running_max[i-1], cumulative[i]);
    }
    
    // Calculate drawdown series
    result.drawdown_series.resize(pnl_series.size());
    double max_dd = 0.0;
    int max_dd_start = 0;
    int max_dd_end = 0;
    int current_dd_start = 0;
    bool in_drawdown = false;
    
    for (size_t i = 0; i < cumulative.size(); ++i) {
        double dd = running_max[i] - cumulative[i];
        result.drawdown_series[i] = dd;
        
        if (dd > 0 && !in_drawdown) {
            in_drawdown = true;
            current_dd_start = static_cast<int>(i);
        } else if (dd == 0 && in_drawdown) {
            in_drawdown = false;
        }
        
        if (dd > max_dd) {
            max_dd = dd;
            max_dd_start = current_dd_start;
            max_dd_end = static_cast<int>(i);
        }
    }
    
    result.current_drawdown = result.drawdown_series.back();
    result.max_drawdown = max_dd;
    result.max_drawdown_duration = static_cast<double>(max_dd_end - max_dd_start);
    
    // Recovery factor
    double total_return = cumulative.back();
    result.recovery_factor = max_dd > 0 ? total_return / max_dd : 0.0;
    
    return result;
}

} // namespace payoff::engine
