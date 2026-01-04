#pragma once
/**
 * @file bar_builder.hpp
 * @brief OHLCV bar construction from ticks
 */

#include "core/models.hpp"
#include <chrono>
#include <functional>
#include <optional>
#include <unordered_map>

namespace payoff::resampling {

using namespace payoff::core;

// ============================================================================
// Bar Structure
// ============================================================================
struct Bar {
    uint32_t instrument_id = 0;
    std::string symbol;
    Timestamp start_time{0};
    Timestamp end_time{0};
    
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    int64_t volume = 0;
    int64_t tick_count = 0;
    
    // Best bid/ask at close
    std::optional<double> close_bid;
    std::optional<double> close_ask;
    
    [[nodiscard]] bool is_valid() const noexcept {
        return open > 0 && high >= low && tick_count > 0;
    }
};

// ============================================================================
// Bar Builder
// ============================================================================
class BarBuilder {
public:
    using BarCallback = std::function<void(const Bar&)>;
    
    /**
     * @param interval Bar interval (e.g., 1s, 5s, 1min)
     */
    explicit BarBuilder(std::chrono::milliseconds interval);
    
    /**
     * @brief Process incoming snapshot
     * @param snapshot Market snapshot
     * @param callback Called when bar completes
     */
    void on_snapshot(const DepthSnapshot& snapshot, BarCallback callback);
    
    /**
     * @brief Force close current bars (e.g., at market close)
     */
    void flush(BarCallback callback);
    
    /**
     * @brief Get current incomplete bar for a symbol
     */
    [[nodiscard]] std::optional<Bar> get_current(const std::string& symbol) const;
    
    /**
     * @brief Clear all state
     */
    void clear();

private:
    std::chrono::milliseconds interval_;
    std::unordered_map<std::string, Bar> current_bars_;
    std::unordered_map<std::string, Timestamp> bar_start_times_;
    
    void update_bar(Bar& bar, const DepthSnapshot& snapshot);
    [[nodiscard]] Timestamp compute_bar_start(Timestamp ts) const;
};

} // namespace payoff::resampling
