#pragma once
/**
 * @file pricing.hpp
 * @brief Black-Scholes option pricing and Greeks
 * 
 * Pure stateless functions - no side effects, fully deterministic.
 * All inputs in consistent units (prices in currency, time in years, IV in decimal).
 */

#include "payoff/models.hpp"
#include <cmath>
#include <optional>

namespace payoff::engine {

// ============================================================================
// Pricing Parameters
// ============================================================================
struct PricingParams {
    double spot = 0.0;           // Current underlying price
    double strike = 0.0;         // Option strike
    double time_to_expiry = 0.0; // Time to expiry in YEARS
    double volatility = 0.0;     // Implied volatility (decimal, e.g., 0.20 = 20%)
    double risk_free_rate = 0.05; // Risk-free rate (decimal)
    double dividend_yield = 0.0; // Continuous dividend yield
};

// ============================================================================
// Black-Scholes Pricing
// ============================================================================

/**
 * @brief Calculate Black-Scholes call price
 */
[[nodiscard]] double bs_call_price(const PricingParams& params);

/**
 * @brief Calculate Black-Scholes put price
 */
[[nodiscard]] double bs_put_price(const PricingParams& params);

/**
 * @brief Calculate option price (call or put)
 */
[[nodiscard]] double bs_price(const PricingParams& params, OptionType type);

// ============================================================================
// Greeks Calculation
// ============================================================================

/**
 * @brief Calculate all Greeks for an option
 */
[[nodiscard]] Greeks calculate_greeks(const PricingParams& params, OptionType type);

/**
 * @brief Calculate Delta only (faster if only delta needed)
 */
[[nodiscard]] double calculate_delta(const PricingParams& params, OptionType type);

/**
 * @brief Calculate Gamma
 */
[[nodiscard]] double calculate_gamma(const PricingParams& params);

/**
 * @brief Calculate Theta (per day)
 */
[[nodiscard]] double calculate_theta(const PricingParams& params, OptionType type);

/**
 * @brief Calculate Vega (per 1% IV change)
 */
[[nodiscard]] double calculate_vega(const PricingParams& params);

/**
 * @brief Calculate Rho (per 1% rate change)
 */
[[nodiscard]] double calculate_rho(const PricingParams& params, OptionType type);

// ============================================================================
// Implied Volatility
// ============================================================================

/**
 * @brief Calculate implied volatility from option price
 * @param market_price Observed market price
 * @param params Pricing params (volatility field is ignored)
 * @param type Call or Put
 * @param max_iterations Maximum Newton-Raphson iterations
 * @param tolerance Convergence tolerance
 * @return IV if found, nullopt if failed to converge
 */
[[nodiscard]] std::optional<double> calculate_iv(
    double market_price,
    PricingParams params,
    OptionType type,
    int max_iterations = 100,
    double tolerance = 1e-6);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Standard normal CDF
 */
[[nodiscard]] double norm_cdf(double x);

/**
 * @brief Standard normal PDF
 */
[[nodiscard]] double norm_pdf(double x);

/**
 * @brief Calculate d1 term in Black-Scholes
 */
[[nodiscard]] double bs_d1(const PricingParams& params);

/**
 * @brief Calculate d2 term in Black-Scholes
 */
[[nodiscard]] double bs_d2(const PricingParams& params);

/**
 * @brief Convert days to years
 */
[[nodiscard]] constexpr double days_to_years(int days) {
    return static_cast<double>(days) / 365.0;
}

/**
 * @brief Convert years to days
 */
[[nodiscard]] constexpr int years_to_days(double years) {
    return static_cast<int>(years * 365.0);
}

} // namespace payoff::engine
