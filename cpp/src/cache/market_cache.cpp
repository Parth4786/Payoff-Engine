/**
 * @file market_cache.cpp
 * @brief Market cache implementation
 */

#include "cache/market_cache.hpp"
#include <mutex>

namespace payoff::cache {

void MarketCache::update(const DepthSnapshot& snapshot) {
    CachedSnapshot cached;
    cached.ltp = snapshot.trade.last_price;
    cached.volume = snapshot.trade.total_traded_quantity;
    cached.oi = snapshot.trade.open_interest;
    cached.last_update = snapshot.exchange_timestamp;
    cached.best_bid = snapshot.best_bid();
    cached.best_ask = snapshot.best_ask();
    cached.midprice = snapshot.midprice();
    cached.spread = snapshot.spread();
    
    std::unique_lock<std::shared_mutex> lock(mutex_);
    by_id_[snapshot.instrument_id] = cached;
    symbol_to_id_[snapshot.symbol] = snapshot.instrument_id;
}

std::optional<CachedSnapshot> MarketCache::get(uint32_t instrument_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = by_id_.find(instrument_id);
    if (it == by_id_.end()) return std::nullopt;
    return it->second;
}

std::optional<CachedSnapshot> MarketCache::get(const std::string& symbol) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto id_it = symbol_to_id_.find(symbol);
    if (id_it == symbol_to_id_.end()) return std::nullopt;
    
    auto it = by_id_.find(id_it->second);
    if (it == by_id_.end()) return std::nullopt;
    return it->second;
}

std::optional<double> MarketCache::get_ltp(uint32_t instrument_id) const {
    auto cached = get(instrument_id);
    if (!cached || cached->ltp == 0.0) return std::nullopt;
    return cached->ltp;
}

bool MarketCache::contains(uint32_t instrument_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return by_id_.count(instrument_id) > 0;
}

size_t MarketCache::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return by_id_.size();
}

void MarketCache::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    by_id_.clear();
    symbol_to_id_.clear();
}

std::vector<uint32_t> MarketCache::get_instruments() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<uint32_t> result;
    result.reserve(by_id_.size());
    for (const auto& [id, _] : by_id_) {
        result.push_back(id);
    }
    return result;
}

MarketCache& get_market_cache() {
    static MarketCache instance;
    return instance;
}

} // namespace payoff::cache
