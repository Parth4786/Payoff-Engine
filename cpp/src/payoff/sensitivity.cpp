/**
 * @file sensitivity.cpp
 * @brief Sensitivity surface calculations - IV surface, Delta surface, Gamma surface
 * 
 * Calculates option sensitivity across strike/expiry dimensions for visualization.
 */

#include "payoff/calculator.hpp"
#include "payoff/pricing.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace payoff::engine {

// ============================================================================
// SensitivitySurface - 2D grid of option sensitivities
// ============================================================================

struct SurfacePoint {
    double strike;
    double dte;       // Days to expiry
    double value;     // The sensitivity value
    double spot;      // Underlying spot at calculation
};

struct SensitivitySurface {
    std::string name;           // e.g., "IV", "Delta", "Gamma"
    std::string underlying;
    double spot;
    
    std::vector<double> strikes;  // X-axis
    std::vector<double> dtes;     // Y-axis (days to expiry)
    
    // 2D grid: values[dte_idx][strike_idx]
    std::vector<std::vector<double>> values;
    
    // Min/max for color scaling
    double min_value = 0.0;
    double max_value = 0.0;
};

// ============================================================================
// IV Surface Calculator
// ============================================================================

class IVSurfaceCalculator {
public:
    struct Config {
        double spot_range_pct = 20.0;   // +/- 20% from ATM
        int strike_points = 21;          // Number of strike points
        std::vector<int> dte_values = {1, 7, 14, 21, 30, 45, 60, 90};  // Days to expiry
        double risk_free_rate = 0.07;
        double base_iv = 0.20;           // Base IV for synthetic
    };
    
    explicit IVSurfaceCalculator(Config config = {}) : config_(std::move(config)) {}
    
    /**
     * @brief Build IV surface from option chain prices
     * 
     * @param spot Current underlying price
     * @param option_prices Map of (strike, dte, is_call) -> market_price
     * @return IV surface
     */
    SensitivitySurface build_from_prices(
        double spot,
        const std::vector<std::tuple<double, double, bool, double>>& option_prices) const {
        
        SensitivitySurface surface;
        surface.name = "IV";
        surface.spot = spot;
        
        // Generate strike grid
        double range = spot * config_.spot_range_pct / 100.0;
        double min_strike = spot - range;
        double max_strike = spot + range;
        double strike_step = (max_strike - min_strike) / (config_.strike_points - 1);
        
        for (int i = 0; i < config_.strike_points; ++i) {
            surface.strikes.push_back(min_strike + i * strike_step);
        }
        
        // Use configured DTE values
        for (int dte : config_.dte_values) {
            surface.dtes.push_back(static_cast<double>(dte));
        }
        
        // Initialize grid
        surface.values.resize(surface.dtes.size());
        for (auto& row : surface.values) {
            row.resize(surface.strikes.size(), std::nan(""));
        }
        
        // Calculate IV for each point
        surface.min_value = 1e9;
        surface.max_value = -1e9;
        
        for (size_t dte_idx = 0; dte_idx < surface.dtes.size(); ++dte_idx) {
            double dte = surface.dtes[dte_idx];
            double time_to_expiry = dte / 365.0;
            
            for (size_t strike_idx = 0; strike_idx < surface.strikes.size(); ++strike_idx) {
                double strike = surface.strikes[strike_idx];
                
                // Find matching market price if available
                double market_price = find_market_price(option_prices, strike, dte);
                
                double iv = 0.0;
                if (market_price > 0) {
                    // Calculate IV from market price using Newton-Raphson
                    iv = implied_volatility(spot, strike, time_to_expiry, 
                                           market_price, config_.risk_free_rate,
                                           strike <= spot ? OptionType::Put : OptionType::Call);
                } else {
                    // Synthetic IV using simple smile model
                    iv = synthetic_iv(spot, strike, time_to_expiry);
                }
                
                surface.values[dte_idx][strike_idx] = iv;
                
                if (iv > 0 && !std::isnan(iv)) {
                    surface.min_value = std::min(surface.min_value, iv);
                    surface.max_value = std::max(surface.max_value, iv);
                }
            }
        }
        
        return surface;
    }
    
    /**
     * @brief Build synthetic IV surface (for testing/demo)
     */
    SensitivitySurface build_synthetic(double spot) const {
        std::vector<std::tuple<double, double, bool, double>> empty_prices;
        return build_from_prices(spot, empty_prices);
    }

private:
    Config config_;
    
    double find_market_price(
        const std::vector<std::tuple<double, double, bool, double>>& prices,
        double strike, double dte) const {
        
        for (const auto& [s, d, is_call, price] : prices) {
            if (std::abs(s - strike) < 0.01 && std::abs(d - dte) < 0.5) {
                return price;
            }
        }
        return 0.0;
    }
    
    double implied_volatility(double spot, double strike, double time_to_expiry,
                              double market_price, double rate, OptionType type) const {
        // Newton-Raphson IV solver
        double iv = 0.20;  // Initial guess
        
        for (int iter = 0; iter < 50; ++iter) {
            PricingParams params;
            params.spot = spot;
            params.strike = strike;
            params.time_to_expiry = time_to_expiry;
            params.volatility = iv;
            params.risk_free_rate = rate;
            
            double model_price = bs_price(params, type);
            double vega = calculate_greeks(params, type).vega;
            
            double error = model_price - market_price;
            if (std::abs(error) < 1e-6) break;
            if (std::abs(vega) < 1e-10) break;
            
            iv -= error / vega;
            iv = std::max(0.01, std::min(5.0, iv));  // Clamp
        }
        
        return iv;
    }
    
    double synthetic_iv(double spot, double strike, double time_to_expiry) const {
        // Simple volatility smile model
        double moneyness = std::log(strike / spot);
        double atm_iv = config_.base_iv;
        
        // Quadratic smile
        double skew = 0.1;   // Skew coefficient
        double smile = 0.05; // Smile curvature
        
        double iv = atm_iv + skew * moneyness + smile * moneyness * moneyness;
        
        // Term structure: IV increases with sqrt(time)
        double term_factor = std::sqrt(std::max(time_to_expiry, 0.01) / 0.25);
        iv *= term_factor;
        
        return std::max(0.05, std::min(2.0, iv));
    }
};

// ============================================================================
// Greeks Surface Calculator
// ============================================================================

class GreeksSurfaceCalculator {
public:
    struct Config {
        double spot_range_pct = 20.0;
        int strike_points = 21;
        std::vector<int> dte_values = {1, 7, 14, 21, 30, 45, 60, 90};
        double risk_free_rate = 0.07;
    };
    
    explicit GreeksSurfaceCalculator(Config config = {}) : config_(std::move(config)) {}
    
    /**
     * @brief Build Delta surface
     */
    SensitivitySurface build_delta_surface(
        double spot, double iv, OptionType type = OptionType::Call) const {
        
        return build_greek_surface(spot, iv, type, "Delta",
            [](const Greeks& g) { return g.delta; });
    }
    
    /**
     * @brief Build Gamma surface
     */
    SensitivitySurface build_gamma_surface(
        double spot, double iv, OptionType type = OptionType::Call) const {
        
        return build_greek_surface(spot, iv, type, "Gamma",
            [](const Greeks& g) { return g.gamma; });
    }
    
    /**
     * @brief Build Theta surface
     */
    SensitivitySurface build_theta_surface(
        double spot, double iv, OptionType type = OptionType::Call) const {
        
        return build_greek_surface(spot, iv, type, "Theta",
            [](const Greeks& g) { return g.theta; });
    }
    
    /**
     * @brief Build Vega surface
     */
    SensitivitySurface build_vega_surface(
        double spot, double iv, OptionType type = OptionType::Call) const {
        
        return build_greek_surface(spot, iv, type, "Vega",
            [](const Greeks& g) { return g.vega; });
    }

private:
    Config config_;
    
    template<typename GreekExtractor>
    SensitivitySurface build_greek_surface(
        double spot, double iv, OptionType type,
        const std::string& name, GreekExtractor extractor) const {
        
        SensitivitySurface surface;
        surface.name = name;
        surface.spot = spot;
        
        // Generate strike grid
        double range = spot * config_.spot_range_pct / 100.0;
        double min_strike = spot - range;
        double max_strike = spot + range;
        double strike_step = (max_strike - min_strike) / (config_.strike_points - 1);
        
        for (int i = 0; i < config_.strike_points; ++i) {
            surface.strikes.push_back(min_strike + i * strike_step);
        }
        
        for (int dte : config_.dte_values) {
            surface.dtes.push_back(static_cast<double>(dte));
        }
        
        // Initialize grid
        surface.values.resize(surface.dtes.size());
        for (auto& row : surface.values) {
            row.resize(surface.strikes.size(), 0.0);
        }
        
        surface.min_value = 1e9;
        surface.max_value = -1e9;
        
        for (size_t dte_idx = 0; dte_idx < surface.dtes.size(); ++dte_idx) {
            double dte = surface.dtes[dte_idx];
            double time_to_expiry = std::max(dte / 365.0, 0.001);
            
            for (size_t strike_idx = 0; strike_idx < surface.strikes.size(); ++strike_idx) {
                double strike = surface.strikes[strike_idx];
                
                PricingParams params;
                params.spot = spot;
                params.strike = strike;
                params.time_to_expiry = time_to_expiry;
                params.volatility = iv;
                params.risk_free_rate = config_.risk_free_rate;
                
                Greeks greeks = calculate_greeks(params, type);
                double value = extractor(greeks);
                
                surface.values[dte_idx][strike_idx] = value;
                
                if (!std::isnan(value) && !std::isinf(value)) {
                    surface.min_value = std::min(surface.min_value, value);
                    surface.max_value = std::max(surface.max_value, value);
                }
            }
        }
        
        return surface;
    }
};

// ============================================================================
// P&L Sensitivity - What-If Analysis
// ============================================================================

struct PnLSensitivity {
    double spot_up_1pct;
    double spot_down_1pct;
    double iv_up_5pct;
    double iv_down_5pct;
    double theta_1day;
    double combined_worst;  // Worst case scenario
};

/**
 * @brief Calculate P&L sensitivity for a strategy
 */
PnLSensitivity calculate_pnl_sensitivity(
    PayoffCalculator& calc,
    const Strategy& strategy,
    double current_iv) {
    
    PnLSensitivity sens = {};
    
    // Base case
    Scenario base;
    base.days_forward = 0;
    base.spot_shift_pct = 0;
    base.iv_shift_pct = 0;
    auto base_curve = calc.calculate_scenario_payoff(strategy, base, current_iv);
    double base_pnl = 0.0;
    
    // Find PnL at current spot
    for (const auto& pt : base_curve.points) {
        if (std::abs(pt.spot - strategy.underlying_price) < strategy.underlying_price * 0.001) {
            base_pnl = pt.pnl;
            break;
        }
    }
    
    // Spot +1%
    Scenario up1;
    up1.days_forward = 0;
    up1.spot_shift_pct = 1.0;
    up1.iv_shift_pct = 0;
    auto up1_curve = calc.calculate_scenario_payoff(strategy, up1, current_iv);
    for (const auto& pt : up1_curve.points) {
        if (std::abs(pt.spot - strategy.underlying_price) < strategy.underlying_price * 0.001) {
            sens.spot_up_1pct = pt.pnl - base_pnl;
            break;
        }
    }
    
    // Spot -1%
    Scenario down1;
    down1.days_forward = 0;
    down1.spot_shift_pct = -1.0;
    down1.iv_shift_pct = 0;
    auto down1_curve = calc.calculate_scenario_payoff(strategy, down1, current_iv);
    for (const auto& pt : down1_curve.points) {
        if (std::abs(pt.spot - strategy.underlying_price) < strategy.underlying_price * 0.001) {
            sens.spot_down_1pct = pt.pnl - base_pnl;
            break;
        }
    }
    
    // IV +5%
    Scenario iv_up;
    iv_up.days_forward = 0;
    iv_up.spot_shift_pct = 0;
    iv_up.iv_shift_pct = 5.0;
    auto iv_up_curve = calc.calculate_scenario_payoff(strategy, iv_up, current_iv);
    for (const auto& pt : iv_up_curve.points) {
        if (std::abs(pt.spot - strategy.underlying_price) < strategy.underlying_price * 0.001) {
            sens.iv_up_5pct = pt.pnl - base_pnl;
            break;
        }
    }
    
    // IV -5%
    Scenario iv_down;
    iv_down.days_forward = 0;
    iv_down.spot_shift_pct = 0;
    iv_down.iv_shift_pct = -5.0;
    auto iv_down_curve = calc.calculate_scenario_payoff(strategy, iv_down, current_iv);
    for (const auto& pt : iv_down_curve.points) {
        if (std::abs(pt.spot - strategy.underlying_price) < strategy.underlying_price * 0.001) {
            sens.iv_down_5pct = pt.pnl - base_pnl;
            break;
        }
    }
    
    // 1 day theta
    Scenario next_day;
    next_day.days_forward = 1;
    next_day.spot_shift_pct = 0;
    next_day.iv_shift_pct = 0;
    auto next_day_curve = calc.calculate_scenario_payoff(strategy, next_day, current_iv);
    for (const auto& pt : next_day_curve.points) {
        if (std::abs(pt.spot - strategy.underlying_price) < strategy.underlying_price * 0.001) {
            sens.theta_1day = pt.pnl - base_pnl;
            break;
        }
    }
    
    // Worst case: down move + IV crush
    sens.combined_worst = std::min({
        sens.spot_up_1pct,
        sens.spot_down_1pct,
        sens.iv_up_5pct,
        sens.iv_down_5pct,
        sens.theta_1day
    });
    
    return sens;
}

} // namespace payoff::engine
