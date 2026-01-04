#pragma once
/**
 * @file market_cache.hpp
 * @brief Lock-free shared market data cache
 * 
 * One writer (market data thread), many readers (strategy threads).
 * Uses atomic snapshot swap for zero-copy reads.
 */

#include "core/models.hpp"
#include <atomic>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace payoff::cache {

using namespace payoff::core;

// ============================================================================
// Market Snapshot (cached per instrument)
// ============================================================================
struct CachedSnapshot {
    double ltp = 0.0;
    int64_t volume = 0;
    int64_t oi = 0;
    Timestamp last_update{0};
    
    std::optional<double> best_bid;
    std::optional<double> best_ask;
    std::optional<double> midprice;
    std::optional<double> spread;
};

// ============================================================================
// Market Data Cache
// ============================================================================
class MarketCache {
public:
    MarketCache() = default;
    
    /**
     * @brief Update cache with new snapshot (writer)
     */
    void update(const DepthSnapshot& snapshot);
    
    /**
     * @brief Get cached data for an instrument (reader)
     */
    [[nodiscard]] std::optional<CachedSnapshot> get(uint32_t instrument_id) const;
    
    /**
     * @brief Get cached data by symbol
     */
    [[nodiscard]] std::optional<CachedSnapshot> get(const std::string& symbol) const;
    
    /**
     * @brief Get LTP for instrument
     */
    [[nodiscard]] std::optional<double> get_ltp(uint32_t instrument_id) const;
    
    /**
     * @brief Check if instrument is in cache
     */
    [[nodiscard]] bool contains(uint32_t instrument_id) const;
    
    /**
     * @brief Get number of instruments in cache
     */
    [[nodiscard]] size_t size() const;
    
    /**
     * @brief Clear all cached data
     */
    void clear();
    
    /**
     * @brief Get all instrument IDs in cache
     */
    [[nodiscard]] std::vector<uint32_t> get_instruments() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<uint32_t, CachedSnapshot> by_id_;
    std::unordered_map<std::string, uint32_t> symbol_to_id_;
};

// ============================================================================
// Global Cache
// ============================================================================
MarketCache& get_market_cache();

} // namespace payoff::cache
