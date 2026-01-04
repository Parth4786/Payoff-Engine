#pragma once
/**
 * @file calculator.hpp
 * @brief Payoff curve and scenario calculation
 * 
 * Calculates:
 * - Expiry payoff curves
 * - Time-dependent payoff (today, T+N)
 * - Scenario analysis (IV shifts, spot shifts)
 * - Breakevens, max profit/loss
 */

#include "payoff/models.hpp"
#include "payoff/pricing.hpp"
#include <chrono>
#include <vector>

namespace payoff::engine {

// ============================================================================
// Calculator Configuration
// ============================================================================
struct CalculatorConfig {
    // Spot range for payoff curve (as % of current spot)
    double spot_range_pct = 20.0;      // ±20% from current
    int spot_points = 101;              // Number of points on curve
    
    // IV for pricing (if not provided per leg)
    double default_iv = 0.20;           // 20%
    
    // Risk-free rate
    double risk_free_rate = 0.05;       // 5%
    
    // Probability calculation params
    double prob_days_forward = 0;       // Days for probability calc
    
    // Precision
    double breakeven_tolerance = 0.01;  // ₹0.01 precision
};

// ============================================================================
// PayoffCalculator - Main calculation engine
// ============================================================================
class PayoffCalculator {
public:
    explicit PayoffCalculator(CalculatorConfig config = {});
    
    // ========================================================================
    // Single Scenario Calculation
    // ========================================================================
    
    /**
     * @brief Calculate payoff at expiry
     */
    [[nodiscard]] PayoffCurve calculate_expiry_payoff(
        const Strategy& strategy) const;
    
    /**
     * @brief Calculate payoff for a specific scenario
     */
    [[nodiscard]] PayoffCurve calculate_scenario_payoff(
        const Strategy& strategy,
        const Scenario& scenario,
        double iv = 0.20) const;
    
    /**
     * @brief Calculate payoff for today (current IV, full time value)
     */
    [[nodiscard]] PayoffCurve calculate_today_payoff(
        const Strategy& strategy,
        double iv = 0.20) const;
    
    // ========================================================================
    // Multi-Scenario Calculation
    // ========================================================================
    
    /**
     * @brief Calculate multiple scenarios at once
     * @return Vector of payoff curves, one per scenario
     */
    [[nodiscard]] std::vector<PayoffCurve> calculate_scenarios(
        const Strategy& strategy,
        const std::vector<Scenario>& scenarios,
        double iv = 0.20) const;
    
    /**
     * @brief Calculate standard scenario set (Today, T+7, Expiry)
     */
    [[nodiscard]] std::vector<PayoffCurve> calculate_standard_scenarios(
        const Strategy& strategy,
        double iv = 0.20) const;
    
    // ========================================================================
    // Greeks
    // ========================================================================
    
    /**
     * @brief Calculate aggregate Greeks for strategy at current spot
     */
    [[nodiscard]] Greeks calculate_strategy_greeks(
        const Strategy& strategy,
        double iv = 0.20,
        int days_to_expiry = 30) const;
    
    // ========================================================================
    // Breakevens
    // ========================================================================
    
    /**
     * @brief Find breakeven points
     */
    [[nodiscard]] std::vector<double> find_breakevens(
        const PayoffCurve& curve) const;
    
    // ========================================================================
    // Risk Metrics
    // ========================================================================
    
    /**
     * @brief Calculate probability of profit
     * @param strategy The strategy
     * @param iv Implied volatility
     * @param days Days forward for probability calculation
     */
    [[nodiscard]] double calculate_pop(
        const Strategy& strategy,
        double iv,
        int days) const;
    
    /**
     * @brief Calculate expected value
     */
    [[nodiscard]] double calculate_expected_value(
        const Strategy& strategy,
        double iv,
        int days) const;
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    void set_config(CalculatorConfig config) { config_ = config; }
    [[nodiscard]] const CalculatorConfig& config() const { return config_; }
    
    // Exposed for SensitivityCalculator
    [[nodiscard]] double calculate_leg_pnl_at_expiry(
        const OptionLeg& leg, 
        double spot) const;
    
    [[nodiscard]] double calculate_leg_pnl_with_greeks(
        const OptionLeg& leg,
        double spot,
        double iv,
        double time_to_expiry) const;
    
    [[nodiscard]] Greeks calculate_leg_greeks(
        const OptionLeg& leg,
        double spot,
        double iv,
        double time_to_expiry) const;

private:
    CalculatorConfig config_;
    
    [[nodiscard]] std::vector<double> generate_spot_range(
        double current_spot) const;
};

// ============================================================================
// Sensitivity Surface Calculator
// ============================================================================
class SensitivityCalculator {
public:
    /**
     * @brief Calculate Delta surface (Delta vs Spot × Time)
     */
    [[nodiscard]] SensitivitySurface calculate_delta_surface(
        const Strategy& strategy,
        double iv,
        int max_days = 30) const;
    
    /**
     * @brief Calculate Gamma surface
     */
    [[nodiscard]] SensitivitySurface calculate_gamma_surface(
        const Strategy& strategy,
        double iv,
        int max_days = 30) const;
    
    /**
     * @brief Calculate Vega surface (PnL vs Spot × IV)
     */
    [[nodiscard]] SensitivitySurface calculate_vega_surface(
        const Strategy& strategy,
        double base_iv,
        int days_to_expiry = 30) const;
    
    /**
     * @brief Calculate Theta surface (Decay vs Spot × Time)
     */
    [[nodiscard]] SensitivitySurface calculate_theta_surface(
        const Strategy& strategy,
        double iv,
        int max_days = 30) const;
    
    /**
     * @brief Calculate PnL heatmap (PnL vs Spot × IV)
     */
    [[nodiscard]] SensitivitySurface calculate_pnl_surface(
        const Strategy& strategy,
        double base_iv,
        int days_to_expiry = 0) const;

private:
    PayoffCalculator calc_;
};

} // namespace payoff::engine
