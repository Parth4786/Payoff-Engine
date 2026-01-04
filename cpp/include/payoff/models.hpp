#pragma once
/**
 * @file payoff_models.hpp
 * @brief Payoff engine data models
 * 
 * Defines:
 * - OptionLeg: Single option position
 * - Strategy: Collection of legs
 * - Scenario: What-if parameters
 * - PayoffCurve: Output payoff data
 * - Greeks: Delta, Gamma, Theta, Vega, Rho
 */

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace payoff::engine {

// ============================================================================
// Option Side
// ============================================================================
enum class Side : uint8_t {
    Buy = 0,
    Sell = 1
};

enum class OptionType : uint8_t {
    Call = 0,
    Put = 1
};

// ============================================================================
// OptionLeg - Single leg in a strategy
// ============================================================================
struct OptionLeg {
    // Contract details
    OptionType type = OptionType::Call;
    Side side = Side::Buy;
    double strike = 0.0;
    std::chrono::year_month_day expiry;
    
    // Position
    int32_t quantity = 0;        // In lots
    int32_t lot_size = 1;        // Multiplier
    double premium = 0.0;        // Entry price per unit
    
    // Optional: linked instrument
    std::optional<uint32_t> instrument_id;
    std::string symbol;
    
    // ========================================================================
    // Computed
    // ========================================================================
    
    [[nodiscard]] int32_t total_quantity() const noexcept {
        return quantity * lot_size;
    }
    
    [[nodiscard]] double net_premium() const noexcept {
        double mult = (side == Side::Buy) ? -1.0 : 1.0;
        return mult * premium * static_cast<double>(total_quantity());
    }
    
    [[nodiscard]] bool is_long() const noexcept {
        return side == Side::Buy;
    }
    
    [[nodiscard]] bool is_short() const noexcept {
        return side == Side::Sell;
    }
};

// ============================================================================
// Strategy - Collection of option legs
// ============================================================================
struct Strategy {
    std::string name;
    std::string underlying;
    double underlying_price = 0.0;
    
    std::vector<OptionLeg> legs;
    
    // ========================================================================
    // Computed
    // ========================================================================
    
    [[nodiscard]] double total_premium() const noexcept {
        double total = 0.0;
        for (const auto& leg : legs) {
            total += leg.net_premium();
        }
        return total;
    }
    
    [[nodiscard]] bool is_credit() const noexcept {
        return total_premium() > 0.0;
    }
    
    [[nodiscard]] bool is_debit() const noexcept {
        return total_premium() < 0.0;
    }
    
    [[nodiscard]] size_t leg_count() const noexcept {
        return legs.size();
    }
};

// ============================================================================
// Scenario - What-if parameters
// ============================================================================
struct Scenario {
    double spot_shift_pct = 0.0;     // % change in underlying (-10 to +10)
    double iv_shift_pct = 0.0;       // % change in IV (-50 to +50)
    int32_t days_forward = 0;        // Days to advance (0 = today)
    
    std::string description;
    
    [[nodiscard]] static Scenario expiry() {
        Scenario s;
        s.description = "At Expiry";
        s.days_forward = 365;  // Will be clamped to actual expiry
        return s;
    }
    
    [[nodiscard]] static Scenario today() {
        Scenario s;
        s.description = "Today";
        return s;
    }
    
    [[nodiscard]] static Scenario iv_crush(double pct) {
        Scenario s;
        s.iv_shift_pct = pct;
        s.description = "IV " + std::to_string(static_cast<int>(pct)) + "%";
        return s;
    }
};

// ============================================================================
// Greeks - Option sensitivities
// ============================================================================
struct Greeks {
    double delta = 0.0;      // dV/dS
    double gamma = 0.0;      // d²V/dS²
    double theta = 0.0;      // dV/dt (per day)
    double vega = 0.0;       // dV/dσ (per 1% IV change)
    double rho = 0.0;        // dV/dr (per 1% rate change)
    
    // Aggregation
    Greeks& operator+=(const Greeks& other) noexcept {
        delta += other.delta;
        gamma += other.gamma;
        theta += other.theta;
        vega += other.vega;
        rho += other.rho;
        return *this;
    }
    
    [[nodiscard]] Greeks operator+(const Greeks& other) const noexcept {
        Greeks result = *this;
        result += other;
        return result;
    }
    
    [[nodiscard]] Greeks operator*(double scale) const noexcept {
        return {delta * scale, gamma * scale, theta * scale, 
                vega * scale, rho * scale};
    }
};

// ============================================================================
// PayoffPoint - Single point on payoff curve
// ============================================================================
struct PayoffPoint {
    double spot = 0.0;           // Underlying price
    double pnl = 0.0;            // P&L at this spot
    double pnl_pct = 0.0;        // P&L as % of margin/premium
    Greeks greeks;
};

// ============================================================================
// PayoffCurve - Complete payoff output
// ============================================================================
struct PayoffCurve {
    std::string scenario_name;
    std::vector<PayoffPoint> points;
    
    // Summary stats
    double max_profit = 0.0;
    double max_loss = 0.0;
    std::vector<double> breakevens;
    
    // Risk metrics
    double probability_of_profit = 0.0;
    double expected_value = 0.0;
    double tail_loss_5pct = 0.0;     // 5th percentile loss
    double tail_loss_1pct = 0.0;     // 1st percentile loss
    
    // Aggregated Greeks at current spot
    Greeks current_greeks;
};

// ============================================================================
// Sensitivity Surface - 2D heatmap data
// ============================================================================
struct SensitivityCell {
    double x = 0.0;              // e.g., spot
    double y = 0.0;              // e.g., time
    double value = 0.0;          // e.g., delta, PnL
    bool is_kill_zone = false;   // High-risk region
};

struct SensitivitySurface {
    std::string name;            // e.g., "Delta vs Spot×Time"
    std::string x_label;
    std::string y_label;
    std::string value_label;
    
    std::vector<double> x_axis;
    std::vector<double> y_axis;
    std::vector<std::vector<SensitivityCell>> grid;
    
    // Kill zone thresholds
    double kill_zone_threshold = 0.0;
};

} // namespace payoff::engine
