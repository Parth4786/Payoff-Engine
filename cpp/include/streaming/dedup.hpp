#pragma once
/**
 * @file dedup.hpp
 * @brief Duplicate and out-of-order detection
 * 
 * Drops:
 * - Duplicate snapshots (same symbol + exchange_timestamp)
 * - Out-of-order snapshots (exchange_timestamp < last_seen)
 * 
 * Tracks drop counts for observability.
 * 
 * Reference: docs/Market-observatory/streaming/dedup.py
 */

#include "core/models.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

namespace payoff::streaming {

using namespace payoff::core;

// ============================================================================
// Dedup Statistics - Copyable snapshot
// ============================================================================
struct DedupStatsSnapshot {
    uint64_t total_received{0};
    uint64_t duplicates_dropped{0};
    uint64_t out_of_order_dropped{0};
    uint64_t passed_through{0};
    
    [[nodiscard]] double duplicate_rate() const noexcept {
        if (total_received == 0) return 0.0;
        return static_cast<double>(duplicates_dropped) / 
               static_cast<double>(total_received);
    }
    
    [[nodiscard]] double ooo_rate() const noexcept {
        if (total_received == 0) return 0.0;
        return static_cast<double>(out_of_order_dropped) / 
               static_cast<double>(total_received);
    }
};

// ============================================================================
// Dedup Statistics - Thread-safe counter
// ============================================================================
struct DedupStats {
    std::atomic<uint64_t> total_received{0};
    std::atomic<uint64_t> duplicates_dropped{0};
    std::atomic<uint64_t> out_of_order_dropped{0};
    std::atomic<uint64_t> passed_through{0};
    
    [[nodiscard]] DedupStatsSnapshot snapshot() const noexcept {
        DedupStatsSnapshot s;
        s.total_received = total_received.load();
        s.duplicates_dropped = duplicates_dropped.load();
        s.out_of_order_dropped = out_of_order_dropped.load();
        s.passed_through = passed_through.load();
        return s;
    }
    
    void reset() noexcept {
        total_received = 0;
        duplicates_dropped = 0;
        out_of_order_dropped = 0;
        passed_through = 0;
    }
};

// ============================================================================
// Drop Reason
// ============================================================================
enum class DropReason : uint8_t {
    None = 0,
    Duplicate = 1,
    OutOfOrder = 2
};

// ============================================================================
// Deduplicator
// ============================================================================
class Deduplicator {
public:
    Deduplicator() = default;
    
    /**
     * @brief Check if snapshot should be processed or dropped
     * @param snapshot Incoming snapshot
     * @return DropReason::None if should process, otherwise reason for drop
     */
    [[nodiscard]] DropReason check(const DepthSnapshot& snapshot);
    
    /**
     * @brief Process snapshot (check + update state if not dropped)
     * @param snapshot Incoming snapshot
     * @return true if should process, false if dropped
     */
    [[nodiscard]] bool process(const DepthSnapshot& snapshot);
    
    /**
     * @brief Get statistics
     */
    [[nodiscard]] const DedupStats& stats() const noexcept { return stats_; }
    
    /**
     * @brief Reset statistics
     */
    void reset_stats() noexcept { stats_.reset(); }
    
    /**
     * @brief Clear all state (timestamps and stats)
     */
    void clear();
    
    /**
     * @brief Get last seen timestamp for a symbol
     */
    [[nodiscard]] std::optional<Timestamp> last_seen(
        const std::string& symbol) const;

private:
    std::unordered_map<std::string, Timestamp> last_timestamps_;
    DedupStats stats_;
    mutable std::mutex mutex_;
};

} // namespace payoff::streaming
