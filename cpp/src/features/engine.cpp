/**
 * @file engine.cpp
 * @brief Features engine implementation
 */

#include "features/engine.hpp"
#include <algorithm>
#include <cmath>

namespace payoff::features {

std::optional<double> compute_midprice(const DepthSnapshot& snapshot) {
    return snapshot.midprice();
}

std::optional<double> compute_spread(const DepthSnapshot& snapshot) {
    return snapshot.spread();
}

double compute_imbalance(const DepthSnapshot& snapshot) {
    if (snapshot.bids.empty() || snapshot.asks.empty()) {
        return 0.0;
    }
    
    double bid_size = static_cast<double>(snapshot.bids.front().size);
    double ask_size = static_cast<double>(snapshot.asks.front().size);
    double total = bid_size + ask_size;
    
    if (total == 0) return 0.0;
    
    return (bid_size - ask_size) / total;
}

std::optional<double> compute_microprice(const DepthSnapshot& snapshot) {
    auto bb = snapshot.best_bid();
    auto ba = snapshot.best_ask();
    
    if (!bb || !ba) return std::nullopt;
    
    if (snapshot.bids.empty() || snapshot.asks.empty()) {
        return std::nullopt;
    }
    
    double bid_size = static_cast<double>(snapshot.bids.front().size);
    double ask_size = static_cast<double>(snapshot.asks.front().size);
    double total = bid_size + ask_size;
    
    if (total == 0) return (*bb + *ba) / 2.0;
    
    // Microprice weights by opposite side
    return (*bb * ask_size + *ba * bid_size) / total;
}

double compute_depth_slope(const DepthSnapshot& snapshot) {
    // Compute how depth changes as we move away from best price
    // Positive slope = depth increases away from mid
    // Negative slope = depth decreases away from mid (thin book)
    
    if (snapshot.bids.size() < 2 && snapshot.asks.size() < 2) {
        return 0.0;
    }
    
    double slope = 0.0;
    int count = 0;
    
    // Bid side slope
    for (size_t i = 1; i < snapshot.bids.size(); ++i) {
        double price_diff = snapshot.bids[i-1].price - snapshot.bids[i].price;
        double size_diff = static_cast<double>(snapshot.bids[i].size - 
                                                snapshot.bids[i-1].size);
        if (price_diff != 0) {
            slope += size_diff / price_diff;
            ++count;
        }
    }
    
    // Ask side slope
    for (size_t i = 1; i < snapshot.asks.size(); ++i) {
        double price_diff = snapshot.asks[i].price - snapshot.asks[i-1].price;
        double size_diff = static_cast<double>(snapshot.asks[i].size - 
                                                snapshot.asks[i-1].size);
        if (price_diff != 0) {
            slope += size_diff / price_diff;
            ++count;
        }
    }
    
    return count > 0 ? slope / count : 0.0;
}

double compute_ofi(
    const DepthSnapshot& current,
    const DepthSnapshot& prev) {
    
    // Order Flow Imbalance = change in bid side - change in ask side
    double bid_change = 0.0;
    double ask_change = 0.0;
    
    if (!current.bids.empty() && !prev.bids.empty()) {
        bid_change = static_cast<double>(current.bids.front().size) - 
                     static_cast<double>(prev.bids.front().size);
    }
    
    if (!current.asks.empty() && !prev.asks.empty()) {
        ask_change = static_cast<double>(current.asks.front().size) - 
                     static_cast<double>(prev.asks.front().size);
    }
    
    return bid_change - ask_change;
}

double compute_shock(
    const DepthSnapshot& current,
    const std::optional<DepthSnapshot>& prev) {
    
    if (!prev) return 0.0;
    
    auto curr_mid = current.midprice();
    auto prev_mid = prev->midprice();
    
    if (!curr_mid || !prev_mid) return 0.0;
    
    // Shock = absolute percentage change in midprice
    return std::abs(*curr_mid - *prev_mid) / *prev_mid * 100.0;
}

FeatureSnapshot compute_features(
    const DepthSnapshot& snapshot,
    const std::optional<DepthSnapshot>& prev_snapshot,
    [[maybe_unused]] double tick_size) {
    
    FeatureSnapshot features;
    features.instrument_id = snapshot.instrument_id;
    features.symbol = snapshot.symbol;
    features.timestamp = snapshot.exchange_timestamp;
    features.is_stale = snapshot.is_stale;
    features.is_partial = snapshot.is_partial;
    
    // Price metrics
    auto mid = compute_midprice(snapshot);
    if (mid) {
        features.midprice = *mid;
    }
    
    auto spread = compute_spread(snapshot);
    if (spread) {
        features.spread = *spread;
        if (features.midprice > 0) {
            features.spread_bps = (*spread / features.midprice) * 10000.0;
        }
    }
    
    // Imbalance
    features.bid_ask_imbalance = compute_imbalance(snapshot);
    
    auto micro = compute_microprice(snapshot);
    if (micro) {
        features.microprice = *micro;
    }
    
    // Depth
    features.bid_depth = static_cast<double>(snapshot.total_bid_size());
    features.ask_depth = static_cast<double>(snapshot.total_ask_size());
    features.depth_slope = compute_depth_slope(snapshot);
    
    // LPI (simplified)
    double total_depth = features.bid_depth + features.ask_depth;
    features.lpi = total_depth > 0 ? 
        std::min(features.bid_depth, features.ask_depth) / total_depth : 0.0;
    
    // OFI
    if (prev_snapshot) {
        features.ofi = compute_ofi(snapshot, *prev_snapshot);
    }
    
    // Shock
    features.shock = compute_shock(snapshot, prev_snapshot);
    
    return features;
}

} // namespace payoff::features
