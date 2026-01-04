/**
 * @file gap_detector.cpp
 * @brief Gap detector implementation
 */

#include "streaming/gap_detector.hpp"
#include <algorithm>

namespace payoff::streaming {

GapDetector::GapDetector(GapConfig config)
    : config_(std::move(config)) {}

bool GapDetector::check_and_mark(
    DepthSnapshot& snapshot,
    Timestamp current_time) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    stats_.total_checked++;
    
    bool is_stale = false;
    
    // Check age
    auto age = current_time - snapshot.exchange_timestamp;
    if (age > config_.max_age) {
        is_stale = true;
    }
    
    // Check gap from last update
    auto it = last_updates_.find(snapshot.symbol);
    if (it != last_updates_.end()) {
        auto gap = snapshot.exchange_timestamp - it->second;
        
        // Update max gap stat
        int64_t gap_ms = gap.count();
        int64_t current_max = stats_.max_gap_ms.load();
        while (gap_ms > current_max) {
            if (stats_.max_gap_ms.compare_exchange_weak(current_max, gap_ms)) {
                break;
            }
        }
        
        if (gap > config_.gap_threshold) {
            stats_.gaps_detected++;
            is_stale = true;
        }
    }
    
    // Update last update time
    last_updates_[snapshot.symbol] = snapshot.exchange_timestamp;
    
    if (is_stale) {
        snapshot.is_stale = true;
        stats_.stale_marked++;
    }
    
    return is_stale;
}

std::optional<std::chrono::milliseconds> GapDetector::check_gap(
    const std::string& symbol,
    Timestamp current_time) const {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = last_updates_.find(symbol);
    if (it == last_updates_.end()) {
        return std::nullopt;
    }
    
    auto gap = current_time - it->second;
    if (gap > config_.gap_threshold) {
        return gap;
    }
    
    return std::nullopt;
}

std::optional<Timestamp> GapDetector::last_update(
    const std::string& symbol) const {
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = last_updates_.find(symbol);
    if (it == last_updates_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void GapDetector::update_timestamp(const std::string& symbol, Timestamp ts) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_updates_[symbol] = ts;
}

void GapDetector::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_updates_.clear();
    stats_.reset();
}

} // namespace payoff::streaming
