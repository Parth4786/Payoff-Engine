/**
 * @file pricing.cpp
 * @brief Black-Scholes option pricing implementation
 */

#include "payoff/pricing.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace payoff::engine {

// ============================================================================
// Mathematical Constants
// ============================================================================
constexpr double PI = std::numbers::pi;
constexpr double SQRT_2PI = 2.5066282746310002;  // sqrt(2*pi)

// ============================================================================
// Normal Distribution Functions
// ============================================================================

double norm_pdf(double x) {
    return std::exp(-0.5 * x * x) / SQRT_2PI;
}

double norm_cdf(double x) {
    // Abramowitz and Stegun approximation
    constexpr double a1 =  0.254829592;
    constexpr double a2 = -0.284496736;
    constexpr double a3 =  1.421413741;
    constexpr double a4 = -1.453152027;
    constexpr double a5 =  1.061405429;
    constexpr double p  =  0.3275911;
    
    int sign = (x < 0) ? -1 : 1;
    x = std::fabs(x);
    
    double t = 1.0 / (1.0 + p * x);
    double y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * 
               std::exp(-x * x / 2.0);
    
    return 0.5 * (1.0 + sign * y);
}

// ============================================================================
// Black-Scholes d1 and d2
// ============================================================================

double bs_d1(const PricingParams& params) {
    if (params.time_to_expiry <= 0 || params.volatility <= 0) {
        return 0.0;
    }
    
    double vol_sqrt_t = params.volatility * std::sqrt(params.time_to_expiry);
    double d1 = (std::log(params.spot / params.strike) + 
                 (params.risk_free_rate - params.dividend_yield + 
                  0.5 * params.volatility * params.volatility) * 
                 params.time_to_expiry) / vol_sqrt_t;
    return d1;
}

double bs_d2(const PricingParams& params) {
    double d1 = bs_d1(params);
    return d1 - params.volatility * std::sqrt(params.time_to_expiry);
}

// ============================================================================
// Black-Scholes Pricing
// ============================================================================

double bs_call_price(const PricingParams& params) {
    if (params.time_to_expiry <= 0) {
        // At expiry, intrinsic value only
        return std::max(0.0, params.spot - params.strike);
    }
    
    double d1 = bs_d1(params);
    double d2 = bs_d2(params);
    
    double discount = std::exp(-params.risk_free_rate * params.time_to_expiry);
    double div_discount = std::exp(-params.dividend_yield * params.time_to_expiry);
    
    return params.spot * div_discount * norm_cdf(d1) - 
           params.strike * discount * norm_cdf(d2);
}

double bs_put_price(const PricingParams& params) {
    if (params.time_to_expiry <= 0) {
        // At expiry, intrinsic value only
        return std::max(0.0, params.strike - params.spot);
    }
    
    double d1 = bs_d1(params);
    double d2 = bs_d2(params);
    
    double discount = std::exp(-params.risk_free_rate * params.time_to_expiry);
    double div_discount = std::exp(-params.dividend_yield * params.time_to_expiry);
    
    return params.strike * discount * norm_cdf(-d2) - 
           params.spot * div_discount * norm_cdf(-d1);
}

double bs_price(const PricingParams& params, OptionType type) {
    return (type == OptionType::Call) ? 
           bs_call_price(params) : bs_put_price(params);
}

// ============================================================================
// Greeks Calculation
// ============================================================================

double calculate_delta(const PricingParams& params, OptionType type) {
    if (params.time_to_expiry <= 0) {
        // At expiry
        if (type == OptionType::Call) {
            return (params.spot > params.strike) ? 1.0 : 0.0;
        } else {
            return (params.spot < params.strike) ? -1.0 : 0.0;
        }
    }
    
    double d1 = bs_d1(params);
    double div_discount = std::exp(-params.dividend_yield * params.time_to_expiry);
    
    if (type == OptionType::Call) {
        return div_discount * norm_cdf(d1);
    } else {
        return div_discount * (norm_cdf(d1) - 1.0);
    }
}

double calculate_gamma(const PricingParams& params) {
    if (params.time_to_expiry <= 0 || params.volatility <= 0) {
        return 0.0;
    }
    
    double d1 = bs_d1(params);
    double div_discount = std::exp(-params.dividend_yield * params.time_to_expiry);
    
    return div_discount * norm_pdf(d1) / 
           (params.spot * params.volatility * std::sqrt(params.time_to_expiry));
}

double calculate_theta(const PricingParams& params, OptionType type) {
    if (params.time_to_expiry <= 0) {
        return 0.0;
    }
    
    double d1 = bs_d1(params);
    double d2 = bs_d2(params);
    double sqrt_t = std::sqrt(params.time_to_expiry);
    double discount = std::exp(-params.risk_free_rate * params.time_to_expiry);
    double div_discount = std::exp(-params.dividend_yield * params.time_to_expiry);
    
    double term1 = -params.spot * div_discount * norm_pdf(d1) * params.volatility / 
                   (2.0 * sqrt_t);
    
    if (type == OptionType::Call) {
        double term2 = params.dividend_yield * params.spot * div_discount * norm_cdf(d1);
        double term3 = params.risk_free_rate * params.strike * discount * norm_cdf(d2);
        return (term1 - term3 + term2) / 365.0;  // Per day
    } else {
        double term2 = params.dividend_yield * params.spot * div_discount * norm_cdf(-d1);
        double term3 = params.risk_free_rate * params.strike * discount * norm_cdf(-d2);
        return (term1 + term3 - term2) / 365.0;  // Per day
    }
}

double calculate_vega(const PricingParams& params) {
    if (params.time_to_expiry <= 0) {
        return 0.0;
    }
    
    double d1 = bs_d1(params);
    double div_discount = std::exp(-params.dividend_yield * params.time_to_expiry);
    
    // Vega per 1% change in volatility
    return params.spot * div_discount * norm_pdf(d1) * 
           std::sqrt(params.time_to_expiry) * 0.01;
}

double calculate_rho(const PricingParams& params, OptionType type) {
    if (params.time_to_expiry <= 0) {
        return 0.0;
    }
    
    double d2 = bs_d2(params);
    double discount = std::exp(-params.risk_free_rate * params.time_to_expiry);
    
    // Rho per 1% change in rate
    if (type == OptionType::Call) {
        return params.strike * params.time_to_expiry * discount * 
               norm_cdf(d2) * 0.01;
    } else {
        return -params.strike * params.time_to_expiry * discount * 
               norm_cdf(-d2) * 0.01;
    }
}

Greeks calculate_greeks(const PricingParams& params, OptionType type) {
    Greeks g;
    g.delta = calculate_delta(params, type);
    g.gamma = calculate_gamma(params);
    g.theta = calculate_theta(params, type);
    g.vega = calculate_vega(params);
    g.rho = calculate_rho(params, type);
    return g;
}

// ============================================================================
// Implied Volatility (Newton-Raphson)
// ============================================================================

std::optional<double> calculate_iv(
    double market_price,
    PricingParams params,
    OptionType type,
    int max_iterations,
    double tolerance) {
    
    if (market_price <= 0) return std::nullopt;
    
    // Initial guess using Brenner-Subrahmanyam approximation
    double intrinsic = (type == OptionType::Call) ?
        std::max(0.0, params.spot - params.strike) :
        std::max(0.0, params.strike - params.spot);
    
    double time_value = market_price - intrinsic;
    double iv_guess = std::sqrt(2.0 * PI / params.time_to_expiry) * 
                      (time_value / params.spot);
    iv_guess = std::clamp(iv_guess, 0.01, 2.0);
    
    params.volatility = iv_guess;
    
    for (int i = 0; i < max_iterations; ++i) {
        double price = bs_price(params, type);
        double diff = price - market_price;
        
        if (std::abs(diff) < tolerance) {
            return params.volatility;
        }
        
        // Vega for Newton-Raphson step (not scaled)
        double d1 = bs_d1(params);
        double div_discount = std::exp(-params.dividend_yield * params.time_to_expiry);
        double vega = params.spot * div_discount * norm_pdf(d1) * 
                      std::sqrt(params.time_to_expiry);
        
        if (vega < 1e-10) {
            // Vega too small, can't converge
            return std::nullopt;
        }
        
        params.volatility -= diff / vega;
        params.volatility = std::clamp(params.volatility, 0.001, 5.0);
    }
    
    // Failed to converge
    return std::nullopt;
}

} // namespace payoff::engine
