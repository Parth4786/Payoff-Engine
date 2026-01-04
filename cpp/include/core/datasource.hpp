#pragma once
/**
 * @file datasource.hpp
 * @brief Abstract data source interface for market data
 * 
 * Key principle: Replay and streaming use IDENTICAL downstream logic.
 * Both produce DepthSnapshot objects that flow through the same pipeline.
 * 
 * Reference: docs/Market-observatory/datasource.py
 */

#include "core/models.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace payoff::core {

// ============================================================================
// Time Range for Replay
// ============================================================================
struct TimeRange {
    Timestamp start;
    Timestamp end;
    
    [[nodiscard]] bool is_valid() const noexcept {
        return start < end;
    }
    
    [[nodiscard]] auto duration() const noexcept {
        return end - start;
    }
};

// ============================================================================
// Callback Types
// ============================================================================
using SnapshotCallback = std::function<void(const DepthSnapshot&)>;
using ErrorCallback = std::function<void(const std::string& error)>;

// ============================================================================
// MarketDataSource - Abstract Interface
// ============================================================================
class MarketDataSource {
public:
    virtual ~MarketDataSource() = default;
    
    // ========================================================================
    // Metadata
    // ========================================================================
    
    /**
     * @brief Get the source identifier
     */
    [[nodiscard]] virtual Source get_source_type() const noexcept = 0;
    
    /**
     * @brief Get human-readable name
     */
    [[nodiscard]] virtual std::string get_name() const = 0;
    
    /**
     * @brief Check if source is connected/available
     */
    [[nodiscard]] virtual bool is_connected() const noexcept = 0;
    
    // ========================================================================
    // Symbol Management
    // ========================================================================
    
    /**
     * @brief Get list of available symbols
     * @return Vector of canonical symbols (e.g., "NFO:49543")
     */
    [[nodiscard]] virtual std::vector<std::string> get_symbols() const = 0;
    
    /**
     * @brief Subscribe to symbols for streaming
     * @param symbols List of canonical symbols
     * @return True if subscription successful
     */
    virtual bool subscribe(const std::vector<std::string>& symbols) = 0;
    
    /**
     * @brief Unsubscribe from symbols
     */
    virtual void unsubscribe(const std::vector<std::string>& symbols) = 0;
    
    // ========================================================================
    // Replay (Historical)
    // ========================================================================
    
    /**
     * @brief Replay historical snapshots in order
     * 
     * @param symbols Symbols to replay (empty = all)
     * @param range Time range to replay
     * @param callback Called for each snapshot, in exchange_timestamp order
     * @return Number of snapshots replayed
     * 
     * INVARIANT: Snapshots are yielded in strictly ascending exchange_timestamp order.
     * INVARIANT: No future data leakage - callback sees only past/current data.
     */
    virtual size_t replay_snapshots(
        const std::vector<std::string>& symbols,
        const TimeRange& range,
        SnapshotCallback callback) = 0;
    
    // ========================================================================
    // Streaming (Live)
    // ========================================================================
    
    /**
     * @brief Start streaming live snapshots
     * 
     * @param callback Called for each incoming snapshot
     * @param error_callback Called on errors
     * 
     * Streaming continues until stop_streaming() is called.
     */
    virtual void start_streaming(
        SnapshotCallback callback,
        ErrorCallback error_callback = nullptr) = 0;
    
    /**
     * @brief Stop streaming
     */
    virtual void stop_streaming() = 0;
    
    /**
     * @brief Check if currently streaming
     */
    [[nodiscard]] virtual bool is_streaming() const noexcept = 0;
};

// ============================================================================
// Factory
// ============================================================================
std::unique_ptr<MarketDataSource> create_mock_source();
std::unique_ptr<MarketDataSource> create_clickhouse_source(
    const std::string& host,
    uint16_t port,
    const std::string& database);

} // namespace payoff::core
