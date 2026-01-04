/**
 * @file calculator.cpp
 * @brief Payoff calculator implementation
 */

#include "payoff/calculator.hpp"
#include <algorithm>
#include <cmath>

namespace payoff::engine {

// ============================================================================
// PayoffCalculator
// ============================================================================

PayoffCalculator::PayoffCalculator(CalculatorConfig config)
    : config_(std::move(config)) {}

std::vector<double> PayoffCalculator::generate_spot_range(double current_spot) const {
    std::vector<double> spots;
    spots.reserve(static_cast<size_t>(config_.spot_points));
    
    double range = current_spot * config_.spot_range_pct / 100.0;
    double min_spot = current_spot - range;
    double max_spot = current_spot + range;
    double step = (max_spot - min_spot) / static_cast<double>(config_.spot_points - 1);
    
    for (int i = 0; i < config_.spot_points; ++i) {
        spots.push_back(min_spot + static_cast<double>(i) * step);
    }
    
    return spots;
}

double PayoffCalculator::calculate_leg_pnl_at_expiry(
    const OptionLeg& leg, 
    double spot) const {
    
    double intrinsic = 0.0;
    if (leg.type == OptionType::Call) {
        intrinsic = std::max(0.0, spot - leg.strike);
    } else {
        intrinsic = std::max(0.0, leg.strike - spot);
    }
    
    double multiplier = (leg.side == Side::Buy) ? 1.0 : -1.0;
    double pnl = multiplier * (intrinsic - leg.premium) * 
                 static_cast<double>(leg.total_quantity());
    
    return pnl;
}

double PayoffCalculator::calculate_leg_pnl_with_greeks(
    const OptionLeg& leg,
    double spot,
    double iv,
    double time_to_expiry) const {
    
    if (time_to_expiry <= 0) {
        return calculate_leg_pnl_at_expiry(leg, spot);
    }
    
    PricingParams params;
    params.spot = spot;
    params.strike = leg.strike;
    params.time_to_expiry = time_to_expiry;
    params.volatility = iv;
    params.risk_free_rate = config_.risk_free_rate;
    
    double option_price = bs_price(params, leg.type);
    
    double multiplier = (leg.side == Side::Buy) ? 1.0 : -1.0;
    double pnl = multiplier * (option_price - leg.premium) * 
                 static_cast<double>(leg.total_quantity());
    
    return pnl;
}

Greeks PayoffCalculator::calculate_leg_greeks(
    const OptionLeg& leg,
    double spot,
    double iv,
    double time_to_expiry) const {
    
    PricingParams params;
    params.spot = spot;
    params.strike = leg.strike;
    params.time_to_expiry = time_to_expiry;
    params.volatility = iv;
    params.risk_free_rate = config_.risk_free_rate;
    
    Greeks greeks = calculate_greeks(params, leg.type);
    
    // Scale by position
    double multiplier = (leg.side == Side::Buy) ? 1.0 : -1.0;
    double qty = static_cast<double>(leg.total_quantity());
    
    return greeks * (multiplier * qty);
}

PayoffCurve PayoffCalculator::calculate_expiry_payoff(
    const Strategy& strategy) const {
    
    PayoffCurve curve;
    curve.scenario_name = "At Expiry";
    
    auto spots = generate_spot_range(strategy.underlying_price);
    curve.points.reserve(spots.size());
    
    double max_profit = std::numeric_limits<double>::lowest();
    double max_loss = std::numeric_limits<double>::max();
    
    for (double spot : spots) {
        PayoffPoint point;
        point.spot = spot;
        point.pnl = 0.0;
        
        for (const auto& leg : strategy.legs) {
            point.pnl += calculate_leg_pnl_at_expiry(leg, spot);
        }
        
        // Track max profit/loss
        if (point.pnl > max_profit) max_profit = point.pnl;
        if (point.pnl < max_loss) max_loss = point.pnl;
        
        curve.points.push_back(point);
    }
    
    curve.max_profit = max_profit;
    curve.max_loss = max_loss;
    curve.breakevens = find_breakevens(curve);
    
    return curve;
}

PayoffCurve PayoffCalculator::calculate_scenario_payoff(
    const Strategy& strategy,
    const Scenario& scenario,
    double iv) const {
    
    PayoffCurve curve;
    curve.scenario_name = scenario.description.empty() ? 
        "Custom Scenario" : scenario.description;
    
    // Apply scenario adjustments
    double adjusted_iv = iv * (1.0 + scenario.iv_shift_pct / 100.0);
    double time_to_expiry = days_to_years(
        std::max(0, 30 - scenario.days_forward));  // Simplified: assume 30 DTE
    
    auto spots = generate_spot_range(strategy.underlying_price);
    curve.points.reserve(spots.size());
    
    double max_profit = std::numeric_limits<double>::lowest();
    double max_loss = std::numeric_limits<double>::max();
    
    for (double base_spot : spots) {
        // Apply spot shift
        double spot = base_spot * (1.0 + scenario.spot_shift_pct / 100.0);
        
        PayoffPoint point;
        point.spot = base_spot;  // Use original spot for x-axis
        point.pnl = 0.0;
        
        for (const auto& leg : strategy.legs) {
            if (time_to_expiry <= 0) {
                point.pnl += calculate_leg_pnl_at_expiry(leg, spot);
            } else {
                point.pnl += calculate_leg_pnl_with_greeks(
                    leg, spot, adjusted_iv, time_to_expiry);
                point.greeks += calculate_leg_greeks(
                    leg, spot, adjusted_iv, time_to_expiry);
            }
        }
        
        if (point.pnl > max_profit) max_profit = point.pnl;
        if (point.pnl < max_loss) max_loss = point.pnl;
        
        curve.points.push_back(point);
    }
    
    curve.max_profit = max_profit;
    curve.max_loss = max_loss;
    curve.breakevens = find_breakevens(curve);
    
    // Calculate current Greeks (at current spot)
    curve.current_greeks = calculate_strategy_greeks(
        strategy, adjusted_iv, 30 - scenario.days_forward);
    
    return curve;
}

PayoffCurve PayoffCalculator::calculate_today_payoff(
    const Strategy& strategy,
    double iv) const {
    
    return calculate_scenario_payoff(strategy, Scenario::today(), iv);
}

std::vector<PayoffCurve> PayoffCalculator::calculate_scenarios(
    const Strategy& strategy,
    const std::vector<Scenario>& scenarios,
    double iv) const {
    
    std::vector<PayoffCurve> curves;
    curves.reserve(scenarios.size());
    
    for (const auto& scenario : scenarios) {
        curves.push_back(calculate_scenario_payoff(strategy, scenario, iv));
    }
    
    return curves;
}

std::vector<PayoffCurve> PayoffCalculator::calculate_standard_scenarios(
    const Strategy& strategy,
    double iv) const {
    
    std::vector<Scenario> scenarios;
    
    // Today
    scenarios.push_back(Scenario::today());
    
    // T+7
    Scenario t7;
    t7.days_forward = 7;
    t7.description = "T+7";
    scenarios.push_back(t7);
    
    // At Expiry
    scenarios.push_back(Scenario::expiry());
    
    return calculate_scenarios(strategy, scenarios, iv);
}

Greeks PayoffCalculator::calculate_strategy_greeks(
    const Strategy& strategy,
    double iv,
    int days_to_expiry) const {
    
    Greeks total;
    double time_to_expiry = days_to_years(days_to_expiry);
    
    for (const auto& leg : strategy.legs) {
        total += calculate_leg_greeks(
            leg, strategy.underlying_price, iv, time_to_expiry);
    }
    
    return total;
}

std::vector<double> PayoffCalculator::find_breakevens(
    const PayoffCurve& curve) const {
    
    std::vector<double> breakevens;
    
    if (curve.points.size() < 2) return breakevens;
    
    for (size_t i = 1; i < curve.points.size(); ++i) {
        const auto& prev = curve.points[i-1];
        const auto& curr = curve.points[i];
        
        // Check for sign change
        if ((prev.pnl <= 0 && curr.pnl >= 0) || 
            (prev.pnl >= 0 && curr.pnl <= 0)) {
            
            // Linear interpolation
            if (std::abs(curr.pnl - prev.pnl) > config_.breakeven_tolerance) {
                double t = -prev.pnl / (curr.pnl - prev.pnl);
                double breakeven = prev.spot + t * (curr.spot - prev.spot);
                breakevens.push_back(breakeven);
            }
        }
    }
    
    return breakevens;
}

double PayoffCalculator::calculate_pop(
    const Strategy& strategy,
    double iv,
    [[maybe_unused]] int days) const {
    
    // Simplified probability of profit calculation
    // Uses probability that underlying ends up in profitable region
    
    auto curve = calculate_scenario_payoff(strategy, Scenario::expiry(), iv);
    
    int profitable_points = 0;
    for (const auto& point : curve.points) {
        if (point.pnl > 0) ++profitable_points;
    }
    
    return static_cast<double>(profitable_points) / 
           static_cast<double>(curve.points.size());
}

double PayoffCalculator::calculate_expected_value(
    const Strategy& strategy,
    double iv,
    [[maybe_unused]] int days) const {
    
    auto curve = calculate_scenario_payoff(strategy, Scenario::expiry(), iv);
    
    double sum = 0.0;
    for (const auto& point : curve.points) {
        sum += point.pnl;
    }
    
    return sum / static_cast<double>(curve.points.size());
}

// ============================================================================
// SensitivityCalculator
// ============================================================================

SensitivitySurface SensitivityCalculator::calculate_delta_surface(
    const Strategy& strategy,
    double iv,
    int max_days) const {
    
    SensitivitySurface surface;
    surface.name = "Delta Surface";
    surface.x_label = "Spot Price";
    surface.y_label = "Days to Expiry";
    surface.value_label = "Delta";
    
    // Generate axes
    double range = strategy.underlying_price * 0.15;  // ±15%
    int spot_points = 21;
    [[maybe_unused]] int time_points = max_days + 1;
    
    for (int i = 0; i < spot_points; ++i) {
        double spot = strategy.underlying_price - range + 
                      2.0 * range * static_cast<double>(i) / 
                      static_cast<double>(spot_points - 1);
        surface.x_axis.push_back(spot);
    }
    
    for (int d = 0; d <= max_days; ++d) {
        surface.y_axis.push_back(static_cast<double>(d));
    }
    
    // Calculate grid
    surface.grid.resize(surface.y_axis.size());
    
    for (size_t yi = 0; yi < surface.y_axis.size(); ++yi) {
        int days = static_cast<int>(surface.y_axis[yi]);
        double tte = days_to_years(max_days - days);
        
        surface.grid[yi].resize(surface.x_axis.size());
        
        for (size_t xi = 0; xi < surface.x_axis.size(); ++xi) {
            double spot = surface.x_axis[xi];
            
            Greeks greeks;
            for (const auto& leg : strategy.legs) {
                greeks += calc_.calculate_leg_greeks(leg, spot, iv, tte);
            }
            
            SensitivityCell cell;
            cell.x = spot;
            cell.y = static_cast<double>(days);
            cell.value = greeks.delta;
            cell.is_kill_zone = std::abs(greeks.gamma) > 0.01;  // High gamma
            
            surface.grid[yi][xi] = cell;
        }
    }
    
    return surface;
}

SensitivitySurface SensitivityCalculator::calculate_pnl_surface(
    const Strategy& strategy,
    double base_iv,
    int days_to_expiry) const {
    
    SensitivitySurface surface;
    surface.name = "PnL Surface";
    surface.x_label = "Spot Price";
    surface.y_label = "IV (%)";
    surface.value_label = "P&L";
    
    // Generate axes
    double spot_range = strategy.underlying_price * 0.15;
    int spot_points = 21;
    int iv_points = 11;
    
    for (int i = 0; i < spot_points; ++i) {
        double spot = strategy.underlying_price - spot_range + 
                      2.0 * spot_range * static_cast<double>(i) / 
                      static_cast<double>(spot_points - 1);
        surface.x_axis.push_back(spot);
    }
    
    for (int i = 0; i < iv_points; ++i) {
        double iv = base_iv * 0.5 + base_iv * static_cast<double>(i) / 
                    static_cast<double>(iv_points - 1);
        surface.y_axis.push_back(iv * 100.0);  // As percentage
    }
    
    // Calculate grid
    double tte = days_to_years(days_to_expiry);
    surface.grid.resize(surface.y_axis.size());
    
    for (size_t yi = 0; yi < surface.y_axis.size(); ++yi) {
        double iv = surface.y_axis[yi] / 100.0;
        surface.grid[yi].resize(surface.x_axis.size());
        
        for (size_t xi = 0; xi < surface.x_axis.size(); ++xi) {
            double spot = surface.x_axis[xi];
            
            double pnl = 0.0;
            for (const auto& leg : strategy.legs) {
                pnl += calc_.calculate_leg_pnl_with_greeks(leg, spot, iv, tte);
            }
            
            SensitivityCell cell;
            cell.x = spot;
            cell.y = surface.y_axis[yi];
            cell.value = pnl;
            cell.is_kill_zone = pnl < -std::abs(strategy.total_premium()) * 2;
            
            surface.grid[yi][xi] = cell;
        }
    }
    
    return surface;
}

// Implement other surface calculations similarly...
SensitivitySurface SensitivityCalculator::calculate_gamma_surface(
    [[maybe_unused]] const Strategy& strategy,
    [[maybe_unused]] double iv,
    [[maybe_unused]] int max_days) const {
    
    // Similar to delta surface but with gamma values
    SensitivitySurface surface;
    surface.name = "Gamma Surface";
    surface.x_label = "Spot Price";
    surface.y_label = "Days to Expiry";
    surface.value_label = "Gamma";
    // ... implementation similar to delta_surface
    return surface;
}

SensitivitySurface SensitivityCalculator::calculate_vega_surface(
    [[maybe_unused]] const Strategy& strategy,
    [[maybe_unused]] double base_iv,
    [[maybe_unused]] int days_to_expiry) const {
    
    SensitivitySurface surface;
    surface.name = "Vega Surface";
    // ... implementation
    return surface;
}

SensitivitySurface SensitivityCalculator::calculate_theta_surface(
    [[maybe_unused]] const Strategy& strategy,
    [[maybe_unused]] double iv,
    [[maybe_unused]] int max_days) const {
    
    SensitivitySurface surface;
    surface.name = "Theta Surface";
    // ... implementation
    return surface;
}

} // namespace payoff::engine
