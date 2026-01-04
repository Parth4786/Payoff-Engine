#pragma once
/**
 * @file engine.hpp
 * @brief Features computation engine
 * 
 * Pure functions for computing market microstructure features.
 * Reference: docs/Market-observatory/engine.py
 */

#include "core/models.hpp"
#include <optional>

namespace payoff::features {

using namespace payoff::core;

// ============================================================================
// Feature Snapshot
// ============================================================================
struct FeatureSnapshot {
    uint32_t instrument_id = 0;
    std::string symbol;
    Timestamp timestamp{0};
    
    // Price metrics
    double midprice = 0.0;
    double spread = 0.0;
    double spread_bps = 0.0;          // Spread in basis points
    
    // Imbalance metrics
    double bid_ask_imbalance = 0.0;   // (bid_size - ask_size) / (bid_size + ask_size)
    double microprice = 0.0;          // Imbalance-weighted midprice
    
    // Depth metrics
    double bid_depth = 0.0;           // Total bid size (all levels)
    double ask_depth = 0.0;           // Total ask size (all levels)
    double depth_slope = 0.0;         // How depth changes with price
    
    // Liquidity metrics
    double lpi = 0.0;                 // Liquidity Provider Index
    double ofi = 0.0;                 // Order Flow Imbalance (vs prev snapshot)
    
    // Volatility/shock
    double shock = 0.0;               // Price shock indicator
    
    // Quality flags
    bool is_stale = false;
    bool is_partial = false;
};

// ============================================================================
// Feature Engine
// ============================================================================

/**
 * @brief Compute features from market snapshot
 * 
 * Pure function - no side effects, deterministic output.
 * 
 * @param snapshot Current market snapshot
 * @param prev_snapshot Previous snapshot (for OFI calculation)
 * @param tick_size Instrument tick size
 * @return Computed features
 */
[[nodiscard]] FeatureSnapshot compute_features(
    const DepthSnapshot& snapshot,
    const std::optional<DepthSnapshot>& prev_snapshot = std::nullopt,
    double tick_size = 0.05);

/**
 * @brief Compute midprice
 */
[[nodiscard]] std::optional<double> compute_midprice(const DepthSnapshot& snapshot);

/**
 * @brief Compute bid-ask spread
 */
[[nodiscard]] std::optional<double> compute_spread(const DepthSnapshot& snapshot);

/**
 * @brief Compute bid-ask imbalance at best level
 */
[[nodiscard]] double compute_imbalance(const DepthSnapshot& snapshot);

/**
 * @brief Compute microprice (imbalance-adjusted mid)
 */
[[nodiscard]] std::optional<double> compute_microprice(const DepthSnapshot& snapshot);

/**
 * @brief Compute depth slope
 */
[[nodiscard]] double compute_depth_slope(const DepthSnapshot& snapshot);

/**
 * @brief Compute Order Flow Imbalance
 */
[[nodiscard]] double compute_ofi(
    const DepthSnapshot& current,
    const DepthSnapshot& prev);

/**
 * @brief Compute price shock indicator
 */
[[nodiscard]] double compute_shock(
    const DepthSnapshot& current,
    const std::optional<DepthSnapshot>& prev);

} // namespace payoff::features
