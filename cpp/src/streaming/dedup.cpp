/**
 * @file dedup.cpp
 * @brief Deduplicator implementation
 */

#include "streaming/dedup.hpp"

namespace payoff::streaming {

DropReason Deduplicator::check(const DepthSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = last_timestamps_.find(snapshot.symbol);
    
    if (it == last_timestamps_.end()) {
        // First time seeing this symbol
        return DropReason::None;
    }
    
    Timestamp last = it->second;
    
    // Check for duplicate (same timestamp)
    if (snapshot.exchange_timestamp == last) {
        return DropReason::Duplicate;
    }
    
    // Check for out-of-order (older than last seen)
    if (snapshot.exchange_timestamp < last) {
        return DropReason::OutOfOrder;
    }
    
    return DropReason::None;
}

bool Deduplicator::process(const DepthSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    stats_.total_received++;
    
    auto it = last_timestamps_.find(snapshot.symbol);
    
    if (it == last_timestamps_.end()) {
        // First time seeing this symbol
        last_timestamps_[snapshot.symbol] = snapshot.exchange_timestamp;
        stats_.passed_through++;
        return true;
    }
    
    Timestamp last = it->second;
    
    // Check for duplicate
    if (snapshot.exchange_timestamp == last) {
        stats_.duplicates_dropped++;
        return false;
    }
    
    // Check for out-of-order
    if (snapshot.exchange_timestamp < last) {
        stats_.out_of_order_dropped++;
        return false;
    }
    
    // Update timestamp and pass through
    it->second = snapshot.exchange_timestamp;
    stats_.passed_through++;
    return true;
}

void Deduplicator::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_timestamps_.clear();
    stats_.reset();
}

std::optional<Timestamp> Deduplicator::last_seen(
    const std::string& symbol) const {
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = last_timestamps_.find(symbol);
    if (it == last_timestamps_.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace payoff::streaming
