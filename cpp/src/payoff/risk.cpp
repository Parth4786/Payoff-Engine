/**
 * @file risk.cpp
 * @brief Risk calculations - VaR, max drawdown, position limits, kill switch
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

struct VaRConfig {
    int num_simulations = 10000;
    double spot_vol = 0.02;
    double iv_vol = 0.10;
    double correlation = -0.3;
    int holding_period = 1;
};

struct VaRResult {
    double var_95 = 0.0;
    double var_99 = 0.0;
    double cvar_95 = 0.0;
    double max_loss = 0.0;
    int num_simulations = 0;
};

class VaRCalculator {
public:
    VaRConfig config;
    
    VaRCalculator() = default;
    explicit VaRCalculator(VaRConfig cfg) : config(cfg) {}
    
    VaRResult calculate(
        PayoffCalculator& calc,
        const Strategy& strategy,
        double current_iv) const {
        
        std::vector<double> pnls;
        pnls.reserve(config.num_simulations);
        
        std::mt19937 gen(42);
        std::normal_distribution<> norm(0.0, 1.0);
        
        double base_pnl = calculate_strategy_pnl(strategy, 
            strategy.underlying_price, current_iv, 0);
        
        for (int i = 0; i < config.num_simulations; ++i) {
            double z1 = norm(gen);
            double z2 = config.correlation * z1 + 
                       std::sqrt(1 - config.correlation * config.correlation) * norm(gen);
            
            double spot_shock = config.spot_vol * std::sqrt(config.holding_period) * z1;
            double iv_shock = config.iv_vol * z2;
            
            double new_spot = strategy.underlying_price * (1 + spot_shock);
            double new_iv = current_iv * (1 + iv_shock);
            new_iv = std::max(0.05, new_iv);
            
            double sim_pnl = calculate_strategy_pnl(strategy, 
                new_spot, new_iv, config.holding_period);
            
            pnls.push_back(sim_pnl - base_pnl);
        }
        
        std::sort(pnls.begin(), pnls.end());
        
        VaRResult result;
        result.num_simulations = config.num_simulations;
        
        int idx_95 = static_cast<int>(0.05 * config.num_simulations);
        result.var_95 = -pnls[idx_95];
        
        int idx_99 = static_cast<int>(0.01 * config.num_simulations);
        result.var_99 = -pnls[idx_99];
        
        double sum_worst = 0.0;
        for (int i = 0; i < idx_95; ++i) {
            sum_worst += pnls[i];
        }
        result.cvar_95 = -sum_worst / idx_95;
        
        result.max_loss = -pnls.front();
        
        return result;
    }

private:
    double calculate_strategy_pnl(
        const Strategy& strategy,
        double spot,
        double iv,
        int days_forward) const {
        
        double total_pnl = 0.0;
        
        for (const auto& leg : strategy.legs) {
            double time_to_expiry = std::max((30 - days_forward) / 365.0, 0.001);
            
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
    double margin_required = 0.0;
    double margin_used = 0.0;
    double margin_utilization = 0.0;
    double notional_exposure = 0.0;
    double delta_exposure = 0.0;
    double gamma_exposure = 0.0;
    double vega_exposure = 0.0;
    double max_profit = 0.0;
    double max_loss = 0.0;
    double risk_reward_ratio = 0.0;
    double probability_of_profit = 0.0;
    double expected_value = 0.0;
};

PositionRisk calculate_position_risk(
    const Strategy& strategy,
    double current_iv,
    double available_margin) {
    
    PositionRisk risk;
    
    Greeks total_greeks;
    double notional = 0.0;
    
    for (const auto& leg : strategy.legs) {
        double time_to_expiry = 30.0 / 365.0;
        
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
    
    risk.margin_required = std::abs(risk.delta_exposure) * 0.15 +
                          std::abs(risk.gamma_exposure) * strategy.underlying_price * 0.05;
    risk.margin_used = risk.margin_required;
    risk.margin_utilization = available_margin > 0 ? 
        (risk.margin_used / available_margin) * 100.0 : 0.0;
    
    PayoffCalculator calc;
    auto curve = calc.calculate_expiry_payoff(strategy);
    risk.max_profit = curve.max_profit;
    risk.max_loss = curve.max_loss;
    
    risk.risk_reward_ratio = risk.max_loss != 0 ? 
        std::abs(risk.max_profit / risk.max_loss) : 0.0;
    
    risk.probability_of_profit = 0.5 + total_greeks.delta * 0.5;
    risk.probability_of_profit = std::max(0.0, std::min(1.0, risk.probability_of_profit));
    
    risk.expected_value = risk.probability_of_profit * risk.max_profit + 
                         (1 - risk.probability_of_profit) * risk.max_loss;
    
    return risk;
}

// ============================================================================
// Kill Switch
// ============================================================================

struct KillSwitchConfig {
    double max_position_value = 1000000.0;
    double max_margin_utilization = 80.0;
    double max_delta_exposure = 100000.0;
    double max_loss_per_trade = 50000.0;
    double max_daily_loss = 100000.0;
    int max_positions = 50;
    double max_vega_exposure = 50000.0;
};

struct KillSwitchStatus {
    bool is_tripped = false;
    std::vector<std::string> violations;
};

KillSwitchStatus check_kill_switch(
    const PositionRisk& risk,
    double current_loss,
    double daily_loss,
    int position_count,
    const KillSwitchConfig& config) {
    
    KillSwitchStatus status;
    
    if (risk.notional_exposure > config.max_position_value) {
        status.is_tripped = true;
        status.violations.push_back("Position value exceeds limit");
    }
    
    if (risk.margin_utilization > config.max_margin_utilization) {
        status.is_tripped = true;
        status.violations.push_back("Margin utilization exceeds limit");
    }
    
    if (std::abs(risk.delta_exposure) > config.max_delta_exposure) {
        status.is_tripped = true;
        status.violations.push_back("Delta exposure exceeds limit");
    }
    
    if (current_loss > config.max_loss_per_trade) {
        status.is_tripped = true;
        status.violations.push_back("Trade loss exceeds limit");
    }
    
    if (daily_loss > config.max_daily_loss) {
        status.is_tripped = true;
        status.violations.push_back("Daily loss exceeds limit");
    }
    
    if (position_count > config.max_positions) {
        status.is_tripped = true;
        status.violations.push_back("Position count exceeds limit");
    }
    
    if (std::abs(risk.vega_exposure) > config.max_vega_exposure) {
        status.is_tripped = true;
        status.violations.push_back("Vega exposure exceeds limit");
    }
    
    return status;
}

// ============================================================================
// Drawdown Calculator
// ============================================================================

struct DrawdownResult {
    double current_drawdown = 0.0;
    double max_drawdown = 0.0;
    double max_drawdown_duration = 0.0;
    double recovery_factor = 0.0;
    std::vector<double> drawdown_series;
};

DrawdownResult calculate_drawdown(const std::vector<double>& pnl_series) {
    DrawdownResult result;
    
    if (pnl_series.empty()) {
        return result;
    }
    
    std::vector<double> cumulative(pnl_series.size());
    cumulative[0] = pnl_series[0];
    for (size_t i = 1; i < pnl_series.size(); ++i) {
        cumulative[i] = cumulative[i-1] + pnl_series[i];
    }
    
    std::vector<double> running_max(pnl_series.size());
    running_max[0] = cumulative[0];
    for (size_t i = 1; i < cumulative.size(); ++i) {
        running_max[i] = std::max(running_max[i-1], cumulative[i]);
    }
    
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
    
    double total_return = cumulative.back();
    result.recovery_factor = max_dd > 0 ? total_return / max_dd : 0.0;
    
    return result;
}

} // namespace payoff::engine
