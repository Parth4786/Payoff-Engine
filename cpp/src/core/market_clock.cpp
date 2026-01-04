/**
 * @file market_clock.cpp
 * @brief Market clock implementation
 */

#include "core/market_clock.hpp"

namespace payoff::core {

MarketClock::MarketClock(ClockMode mode)
    : mode_(mode) {
    if (mode == ClockMode::Replay) {
        replay_time_ms_ = 0;
    }
}

Timestamp MarketClock::now() const noexcept {
    if (mode_ == ClockMode::Live) {
        auto now_tp = Clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now_tp.time_since_epoch());
        return Timestamp{ms.count()};
    } else {
        return Timestamp{replay_time_ms_.load()};
    }
}

TimePoint MarketClock::now_timepoint() const noexcept {
    if (mode_ == ClockMode::Live) {
        return Clock::now();
    } else {
        return TimePoint{std::chrono::milliseconds{replay_time_ms_.load()}};
    }
}

void MarketClock::set_live_mode() {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = ClockMode::Live;
}

void MarketClock::set_replay_mode(Timestamp start_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = ClockMode::Replay;
    replay_time_ms_ = start_time.count();
}

void MarketClock::advance_to(Timestamp ts) {
    if (mode_ != ClockMode::Replay) return;
    
    // Only advance forward
    int64_t new_time = ts.count();
    int64_t old_time = replay_time_ms_.load();
    
    while (new_time > old_time) {
        if (replay_time_ms_.compare_exchange_weak(old_time, new_time)) {
            break;
        }
    }
}

void MarketClock::set_replay_speed(double speed) {
    std::lock_guard<std::mutex> lock(mutex_);
    replay_speed_ = speed > 0.0 ? speed : 1.0;
}

// ============================================================================
// Global Clock
// ============================================================================

MarketClock& get_market_clock() {
    static MarketClock instance;
    return instance;
}

} // namespace payoff::core
