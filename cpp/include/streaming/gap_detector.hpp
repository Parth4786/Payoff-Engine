#pragma once
/**
 * @file gap_detector.hpp
 * @brief Staleness and gap detection
 * 
 * Marks snapshots as stale when:
 * - Time gap between updates exceeds threshold
 * - Data is older than max age
 * 
 * Reference: docs/Market-observatory/streaming/gap_detector.py
 */

#include "core/models.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace payoff::streaming {

using namespace payoff::core;

// ============================================================================
// Gap Detection Config
// ============================================================================
struct GapConfig {
    // Gap threshold - mark stale if no update for this long
    std::chrono::milliseconds gap_threshold{500};
    
    // Max age - mark stale if data older than this
    std::chrono::milliseconds max_age{5000};
    
    // Grace period after market open
    std::chrono::seconds warmup_period{60};
};

// ============================================================================
// Gap Statistics - Copyable snapshot
// ============================================================================
struct GapStatsSnapshot {
    uint64_t total_checked{0};
    uint64_t gaps_detected{0};
    uint64_t stale_marked{0};
    int64_t max_gap_ms{0};
};

// ============================================================================
// Gap Statistics - Thread-safe counter
// ============================================================================
struct GapStats {
    std::atomic<uint64_t> total_checked{0};
    std::atomic<uint64_t> gaps_detected{0};
    std::atomic<uint64_t> stale_marked{0};
    std::atomic<int64_t> max_gap_ms{0};
    
    [[nodiscard]] GapStatsSnapshot snapshot() const noexcept {
        GapStatsSnapshot s;
        s.total_checked = total_checked.load();
        s.gaps_detected = gaps_detected.load();
        s.stale_marked = stale_marked.load();
        s.max_gap_ms = max_gap_ms.load();
        return s;
    }
    
    void reset() noexcept {
        total_checked = 0;
        gaps_detected = 0;
        stale_marked = 0;
        max_gap_ms = 0;
    }
};

// ============================================================================
// GapDetector
// ============================================================================
class GapDetector {
public:
    explicit GapDetector(GapConfig config = {});
    
    /**
     * @brief Check snapshot for staleness and update state
     * @param snapshot Snapshot to check
     * @param current_time Current market time (for age calculation)
     * @return true if snapshot is stale
     */
    [[nodiscard]] bool check_and_mark(
        DepthSnapshot& snapshot,
        Timestamp current_time);
    
    /**
     * @brief Check if there's a gap for a symbol
     * @param symbol Symbol to check
     * @param current_time Current market time
     * @return Gap duration if gap detected, nullopt otherwise
     */
    [[nodiscard]] std::optional<std::chrono::milliseconds> check_gap(
        const std::string& symbol,
        Timestamp current_time) const;
    
    /**
     * @brief Get last update time for a symbol
     */
    [[nodiscard]] std::optional<Timestamp> last_update(
        const std::string& symbol) const;
    
    /**
     * @brief Update last seen time for a symbol
     */
    void update_timestamp(const std::string& symbol, Timestamp ts);
    
    /**
     * @brief Get statistics
     */
    [[nodiscard]] const GapStats& stats() const noexcept { return stats_; }
    
    /**
     * @brief Reset statistics
     */
    void reset_stats() noexcept { stats_.reset(); }
    
    /**
     * @brief Clear all state
     */
    void clear();
    
    /**
     * @brief Update configuration
     */
    void set_config(GapConfig config) { config_ = config; }
    [[nodiscard]] const GapConfig& config() const { return config_; }

private:
    GapConfig config_;
    std::unordered_map<std::string, Timestamp> last_updates_;
    GapStats stats_;
    mutable std::mutex mutex_;
};

} // namespace payoff::streaming
