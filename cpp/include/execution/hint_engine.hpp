#pragma once
/**
 * @file hint_engine.hpp
 * @brief Execution hint engine
 * 
 * Provides trading posture recommendations based on market microstructure.
 * Reference: docs/Market-observatory/execution/hint_engine.py
 */

#include "features/engine.hpp"
#include <string>
#include <vector>

namespace payoff::execution {

using namespace payoff::features;

// ============================================================================
// Execution Posture
// ============================================================================
enum class Posture : uint8_t {
    Wait = 0,       // Do not trade - conditions unfavorable or uncertain
    Passive = 1,    // Post limit orders, wait for fill
    Aggressive = 2  // Cross spread, take liquidity
};

constexpr const char* posture_to_string(Posture p) {
    switch (p) {
        case Posture::Wait: return "WAIT";
        case Posture::Passive: return "PASSIVE";
        case Posture::Aggressive: return "AGGRESSIVE";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// Execution Hint
// ============================================================================
struct ExecutionHint {
    Posture posture = Posture::Wait;
    std::vector<std::string> reasons;
    double confidence = 0.0;    // 0-1 confidence in the recommendation
    
    // Feature values that drove the decision
    double spread_bps = 0.0;
    double imbalance = 0.0;
    double depth_slope = 0.0;
    double shock = 0.0;
    
    void add_reason(std::string reason) {
        reasons.push_back(std::move(reason));
    }
};

// ============================================================================
// Hint Engine Configuration
// ============================================================================
struct HintEngineConfig {
    // Spread thresholds (in bps)
    double wide_spread_threshold = 20.0;    // Above this = WAIT
    double tight_spread_threshold = 5.0;    // Below this = favor PASSIVE
    
    // Imbalance thresholds
    double strong_imbalance_threshold = 0.5;  // |imbalance| > this = potential edge
    
    // Depth slope thresholds
    double thin_book_threshold = -0.5;       // Negative = thin book = WAIT
    
    // Shock thresholds
    double shock_threshold = 0.5;            // Above this = WAIT
    
    // Staleness
    bool wait_on_stale = true;               // If stale, always WAIT
};

// ============================================================================
// Hint Engine
// ============================================================================
class HintEngine {
public:
    explicit HintEngine(HintEngineConfig config = {});
    
    /**
     * @brief Generate execution hint from features
     * 
     * @param features Current market features
     * @param side Which side to trade (1 = buy, -1 = sell)
     * @return Execution hint with posture and reasons
     */
    [[nodiscard]] ExecutionHint generate_hint(
        const FeatureSnapshot& features,
        int side = 1) const;
    
    /**
     * @brief Update configuration
     */
    void set_config(HintEngineConfig config) { config_ = config; }
    [[nodiscard]] const HintEngineConfig& config() const { return config_; }

private:
    HintEngineConfig config_;
    
    void check_staleness(ExecutionHint& hint, const FeatureSnapshot& features) const;
    void check_spread(ExecutionHint& hint, const FeatureSnapshot& features) const;
    void check_imbalance(ExecutionHint& hint, const FeatureSnapshot& features, int side) const;
    void check_depth(ExecutionHint& hint, const FeatureSnapshot& features) const;
    void check_shock(ExecutionHint& hint, const FeatureSnapshot& features) const;
};

} // namespace payoff::execution
