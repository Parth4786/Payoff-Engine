/**
 * @file hint_engine.cpp
 * @brief Execution hint engine implementation
 */

#include "execution/hint_engine.hpp"
#include <algorithm>
#include <cmath>

namespace payoff::execution {

HintEngine::HintEngine(HintEngineConfig config)
    : config_(std::move(config)) {}

void HintEngine::check_staleness(
    ExecutionHint& hint, 
    const FeatureSnapshot& features) const {
    
    if (config_.wait_on_stale && features.is_stale) {
        hint.posture = Posture::Wait;
        hint.add_reason("Data is stale");
        hint.confidence = 0.0;
    }
    
    if (features.is_partial) {
        hint.add_reason("Partial book data");
        hint.confidence *= 0.5;
    }
}

void HintEngine::check_spread(
    ExecutionHint& hint,
    const FeatureSnapshot& features) const {
    
    hint.spread_bps = features.spread_bps;
    
    if (features.spread_bps > config_.wide_spread_threshold) {
        hint.posture = Posture::Wait;
        hint.add_reason("Spread too wide (" + 
            std::to_string(static_cast<int>(features.spread_bps)) + " bps)");
    } else if (features.spread_bps < config_.tight_spread_threshold) {
        if (hint.posture != Posture::Wait) {
            hint.posture = Posture::Passive;
            hint.add_reason("Tight spread favors passive");
        }
    }
}

void HintEngine::check_imbalance(
    ExecutionHint& hint,
    const FeatureSnapshot& features,
    int side) const {
    
    hint.imbalance = features.bid_ask_imbalance;
    
    // Positive imbalance = more bids = buy pressure
    // For a buyer: positive imbalance is unfavorable (competition)
    // For a seller: positive imbalance is favorable (demand)
    
    double effective_imbalance = features.bid_ask_imbalance * side;
    
    if (std::abs(features.bid_ask_imbalance) > config_.strong_imbalance_threshold) {
        if (effective_imbalance < 0) {
            // Imbalance in our favor
            if (hint.posture != Posture::Wait) {
                hint.posture = Posture::Aggressive;
                hint.add_reason("Strong favorable imbalance");
                hint.confidence += 0.2;
            }
        } else {
            // Imbalance against us
            if (hint.posture == Posture::Aggressive) {
                hint.posture = Posture::Passive;
            }
            hint.add_reason("Imbalance unfavorable - be patient");
            hint.confidence -= 0.1;
        }
    }
}

void HintEngine::check_depth(
    ExecutionHint& hint,
    const FeatureSnapshot& features) const {
    
    hint.depth_slope = features.depth_slope;
    
    if (features.depth_slope < config_.thin_book_threshold) {
        if (hint.posture == Posture::Aggressive) {
            hint.posture = Posture::Passive;
        }
        hint.add_reason("Thin book - risk of slippage");
        hint.confidence -= 0.15;
    }
    
    // Very thin total depth
    double total_depth = features.bid_depth + features.ask_depth;
    if (total_depth < 1000) {  // Configurable threshold
        hint.add_reason("Low total depth");
        hint.confidence -= 0.1;
    }
}

void HintEngine::check_shock(
    ExecutionHint& hint,
    const FeatureSnapshot& features) const {
    
    hint.shock = features.shock;
    
    if (features.shock > config_.shock_threshold) {
        hint.posture = Posture::Wait;
        hint.add_reason("Price shock detected - wait for stability");
        hint.confidence = std::max(0.0, hint.confidence - 0.3);
    }
}

ExecutionHint HintEngine::generate_hint(
    const FeatureSnapshot& features,
    int side) const {
    
    ExecutionHint hint;
    hint.posture = Posture::Passive;  // Default to passive
    hint.confidence = 0.7;            // Base confidence
    
    // Run all checks (order matters - later checks can override)
    check_staleness(hint, features);
    
    // Only continue if not already WAIT from staleness
    if (hint.posture != Posture::Wait || !config_.wait_on_stale) {
        check_spread(hint, features);
        check_shock(hint, features);
        check_depth(hint, features);
        check_imbalance(hint, features, side);
    }
    
    // Clamp confidence
    hint.confidence = std::clamp(hint.confidence, 0.0, 1.0);
    
    // If no clear signal, default to WAIT
    if (hint.reasons.empty()) {
        hint.posture = Posture::Passive;
        hint.add_reason("No strong signals - default passive");
    }
    
    return hint;
}

} // namespace payoff::execution
