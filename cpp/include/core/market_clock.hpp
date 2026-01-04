#pragma once
/**
 * @file market_clock.hpp
 * @brief Unified market clock for live and replay modes
 * 
 * Key principle: Same code path for live and replay.
 * - Live mode: Clock uses system time
 * - Replay mode: Clock advances with replayed data
 * 
 * No future data leakage - strategies see only current/past time.
 */

#include "core/models.hpp"
#include <atomic>
#include <chrono>
#include <mutex>

namespace payoff::core {

// ============================================================================
// Clock Mode
// ============================================================================
enum class ClockMode : uint8_t {
    Live = 0,      // Use system clock
    Replay = 1     // Use simulated time from replay data
};

// ============================================================================
// MarketClock - Thread-safe market time provider
// ============================================================================
class MarketClock {
public:
    explicit MarketClock(ClockMode mode = ClockMode::Live);
    
    // ========================================================================
    // Time Access
    // ========================================================================
    
    /**
     * @brief Get current market time
     * @return Current timestamp (system time in Live, simulated in Replay)
     */
    [[nodiscard]] Timestamp now() const noexcept;
    
    /**
     * @brief Get current time as TimePoint
     */
    [[nodiscard]] TimePoint now_timepoint() const noexcept;
    
    // ========================================================================
    // Mode Control
    // ========================================================================
    
    [[nodiscard]] ClockMode mode() const noexcept { return mode_; }
    
    /**
     * @brief Switch to live mode (uses system clock)
     */
    void set_live_mode();
    
    /**
     * @brief Switch to replay mode with initial time
     */
    void set_replay_mode(Timestamp start_time);
    
    // ========================================================================
    // Replay Time Control
    // ========================================================================
    
    /**
     * @brief Advance replay clock to new time
     * @param ts New timestamp (must be >= current in replay mode)
     * 
     * INVARIANT: Time only moves forward in replay mode.
     */
    void advance_to(Timestamp ts);
    
    /**
     * @brief Get replay speed multiplier
     */
    [[nodiscard]] double replay_speed() const noexcept { return replay_speed_; }
    
    /**
     * @brief Set replay speed (1.0 = real-time, 2.0 = 2x, etc.)
     */
    void set_replay_speed(double speed);

private:
    ClockMode mode_;
    std::atomic<int64_t> replay_time_ms_{0};
    double replay_speed_ = 1.0;
    mutable std::mutex mutex_;
};

// ============================================================================
// Global Clock
// ============================================================================
MarketClock& get_market_clock();

} // namespace payoff::core
