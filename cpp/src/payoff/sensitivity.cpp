/**
 * @file sensitivity.cpp
 * @brief Sensitivity surface calculations - IV surface, Delta surface, Gamma surface
 * 
 * Uses the SensitivitySurface struct from models.hpp.
 */

#include "payoff/calculator.hpp"
#include "payoff/pricing.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace payoff::engine {

// ============================================================================
// IV Surface Calculator
// ============================================================================

class IVSurfaceCalculator {
public:
    struct Config {
        double spot_range_pct = 20.0;
        int strike_points = 21;
        std::vector<int> dte_values = {1, 7, 14, 21, 30, 45, 60, 90};
        double risk_free_rate = 0.07;
        double base_iv = 0.20;
    };
    
    Config config;
    
    IVSurfaceCalculator() = default;
    explicit IVSurfaceCalculator(Config cfg) : config(std::move(cfg)) {}
    
    SensitivitySurface build_from_prices(
        double spot,
        const std::vector<std::tuple<double, double, bool, double>>& option_prices) const {
        
        SensitivitySurface surface;
        surface.name = "IV Surface";
        surface.x_label = "Strike";
        surface.y_label = "DTE";
        surface.value_label = "IV";
        
        double range = spot * config.spot_range_pct / 100.0;
        double min_strike = spot - range;
        double max_strike = spot + range;
        double strike_step = (max_strike - min_strike) / (config.strike_points - 1);
        
        for (int i = 0; i < config.strike_points; ++i) {
            surface.x_axis.push_back(min_strike + i * strike_step);
        }
        
        for (int dte : config.dte_values) {
            surface.y_axis.push_back(static_cast<double>(dte));
        }
        
        surface.grid.resize(surface.y_axis.size());
        for (auto& row : surface.grid) {
            row.resize(surface.x_axis.size());
        }
        
        for (size_t dte_idx = 0; dte_idx < surface.y_axis.size(); ++dte_idx) {
            double dte = surface.y_axis[dte_idx];
            double time_to_expiry = dte / 365.0;
            
            for (size_t strike_idx = 0; strike_idx < surface.x_axis.size(); ++strike_idx) {
                double strike = surface.x_axis[strike_idx];
                double market_price = find_market_price(option_prices, strike, dte);
                
                double iv = 0.0;
                if (market_price > 0) {
                    iv = implied_volatility(spot, strike, time_to_expiry, 
                                           market_price, config.risk_free_rate,
                                           strike <= spot ? OptionType::Put : OptionType::Call);
                } else {
                    iv = synthetic_iv(spot, strike, time_to_expiry);
                }
                
                surface.grid[dte_idx][strike_idx].x = strike;
                surface.grid[dte_idx][strike_idx].y = dte;
                surface.grid[dte_idx][strike_idx].value = iv;
                surface.grid[dte_idx][strike_idx].is_kill_zone = (iv > 1.0);
            }
        }
        
        return surface;
    }
    
    SensitivitySurface build_synthetic(double spot) const {
        std::vector<std::tuple<double, double, bool, double>> empty_prices;
        return build_from_prices(spot, empty_prices);
    }

private:
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
        double iv = 0.20;
        
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
            iv = std::max(0.01, std::min(5.0, iv));
        }
        
        return iv;
    }
    
    double synthetic_iv(double spot, double strike, double time_to_expiry) const {
        double moneyness = std::log(strike / spot);
        double atm_iv = config.base_iv;
        double skew = 0.1;
        double smile = 0.05;
        
        double iv = atm_iv + skew * moneyness + smile * moneyness * moneyness;
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
    
    Config config;
    
    GreeksSurfaceCalculator() = default;
    explicit GreeksSurfaceCalculator(Config cfg) : config(std::move(cfg)) {}
    
    SensitivitySurface build_delta_surface(double spot, double iv, OptionType type = OptionType::Call) const {
        return build_greek_surface(spot, iv, type, "Delta", [](const Greeks& g) { return g.delta; });
    }
    
    SensitivitySurface build_gamma_surface(double spot, double iv, OptionType type = OptionType::Call) const {
        return build_greek_surface(spot, iv, type, "Gamma", [](const Greeks& g) { return g.gamma; });
    }
    
    SensitivitySurface build_theta_surface(double spot, double iv, OptionType type = OptionType::Call) const {
        return build_greek_surface(spot, iv, type, "Theta", [](const Greeks& g) { return g.theta; });
    }
    
    SensitivitySurface build_vega_surface(double spot, double iv, OptionType type = OptionType::Call) const {
        return build_greek_surface(spot, iv, type, "Vega", [](const Greeks& g) { return g.vega; });
    }

private:
    template<typename GreekExtractor>
    SensitivitySurface build_greek_surface(
        double spot, double iv, OptionType type,
        const std::string& name, GreekExtractor extractor) const {
        
        SensitivitySurface surface;
        surface.name = name + " Surface";
        surface.x_label = "Strike";
        surface.y_label = "DTE";
        surface.value_label = name;
        
        double range = spot * config.spot_range_pct / 100.0;
        double min_strike = spot - range;
        double max_strike = spot + range;
        double strike_step = (max_strike - min_strike) / (config.strike_points - 1);
        
        for (int i = 0; i < config.strike_points; ++i) {
            surface.x_axis.push_back(min_strike + i * strike_step);
        }
        
        for (int dte : config.dte_values) {
            surface.y_axis.push_back(static_cast<double>(dte));
        }
        
        surface.grid.resize(surface.y_axis.size());
        for (auto& row : surface.grid) {
            row.resize(surface.x_axis.size());
        }
        
        for (size_t dte_idx = 0; dte_idx < surface.y_axis.size(); ++dte_idx) {
            double dte = surface.y_axis[dte_idx];
            double time_to_expiry = std::max(dte / 365.0, 0.001);
            
            for (size_t strike_idx = 0; strike_idx < surface.x_axis.size(); ++strike_idx) {
                double strike = surface.x_axis[strike_idx];
                
                PricingParams params;
                params.spot = spot;
                params.strike = strike;
                params.time_to_expiry = time_to_expiry;
                params.volatility = iv;
                params.risk_free_rate = config.risk_free_rate;
                
                Greeks greeks = calculate_greeks(params, type);
                double value = extractor(greeks);
                
                surface.grid[dte_idx][strike_idx].x = strike;
                surface.grid[dte_idx][strike_idx].y = dte;
                surface.grid[dte_idx][strike_idx].value = value;
            }
        }
        
        return surface;
    }
};

// ============================================================================
// P&L Sensitivity
// ============================================================================

struct PnLSensitivity {
    double spot_up_1pct = 0.0;
    double spot_down_1pct = 0.0;
    double iv_up_5pct = 0.0;
    double iv_down_5pct = 0.0;
    double theta_1day = 0.0;
    double combined_worst = 0.0;
};

PnLSensitivity calculate_pnl_sensitivity(
    PayoffCalculator& calc,
    const Strategy& strategy,
    double current_iv) {
    
    PnLSensitivity sens;
    
    Scenario base;
    base.days_forward = 0;
    base.spot_shift_pct = 0;
    base.iv_shift_pct = 0;
    auto base_curve = calc.calculate_scenario_payoff(strategy, base, current_iv);
    double base_pnl = 0.0;
    
    for (const auto& pt : base_curve.points) {
        if (std::abs(pt.spot - strategy.underlying_price) < strategy.underlying_price * 0.001) {
            base_pnl = pt.pnl;
            break;
        }
    }
    
    auto get_pnl = [&](int days, double spot_pct, double iv_pct) {
        Scenario sc;
        sc.days_forward = days;
        sc.spot_shift_pct = spot_pct;
        sc.iv_shift_pct = iv_pct;
        auto curve = calc.calculate_scenario_payoff(strategy, sc, current_iv);
        for (const auto& pt : curve.points) {
            if (std::abs(pt.spot - strategy.underlying_price) < strategy.underlying_price * 0.001) {
                return pt.pnl - base_pnl;
            }
        }
        return 0.0;
    };
    
    sens.spot_up_1pct = get_pnl(0, 1.0, 0);
    sens.spot_down_1pct = get_pnl(0, -1.0, 0);
    sens.iv_up_5pct = get_pnl(0, 0, 5.0);
    sens.iv_down_5pct = get_pnl(0, 0, -5.0);
    sens.theta_1day = get_pnl(1, 0, 0);
    
    sens.combined_worst = std::min({
        sens.spot_up_1pct, sens.spot_down_1pct,
        sens.iv_up_5pct, sens.iv_down_5pct, sens.theta_1day
    });
    
    return sens;
}

} // namespace payoff::engine
