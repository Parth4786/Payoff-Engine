/**
 * @file bar_builder.cpp
 * @brief Bar builder implementation
 */

#include "resampling/bar_builder.hpp"
#include <algorithm>

namespace payoff::resampling {

BarBuilder::BarBuilder(std::chrono::milliseconds interval)
    : interval_(interval) {}

Timestamp BarBuilder::compute_bar_start(Timestamp ts) const {
    int64_t ms = ts.count();
    int64_t interval_ms = interval_.count();
    int64_t bar_start = (ms / interval_ms) * interval_ms;
    return Timestamp{bar_start};
}

void BarBuilder::update_bar(Bar& bar, const DepthSnapshot& snapshot) {
    double price = snapshot.trade.last_price;
    
    if (bar.tick_count == 0) {
        // First tick in bar
        bar.open = price;
        bar.high = price;
        bar.low = price;
    } else {
        bar.high = std::max(bar.high, price);
        bar.low = std::min(bar.low, price);
    }
    
    bar.close = price;
    bar.volume += snapshot.trade.last_traded_quantity;
    bar.tick_count++;
    bar.end_time = snapshot.exchange_timestamp;
    bar.close_bid = snapshot.best_bid();
    bar.close_ask = snapshot.best_ask();
}

void BarBuilder::on_snapshot(const DepthSnapshot& snapshot, BarCallback callback) {
    if (!snapshot.trade.has_trade()) return;
    
    Timestamp bar_start = compute_bar_start(snapshot.exchange_timestamp);
    auto& current = current_bars_[snapshot.symbol];
    auto& start_time = bar_start_times_[snapshot.symbol];
    
    // Check if we need to start a new bar
    if (current.tick_count > 0 && bar_start != start_time) {
        // Complete the previous bar
        if (callback) {
            callback(current);
        }
        
        // Reset for new bar
        current = Bar{};
        current.instrument_id = snapshot.instrument_id;
        current.symbol = snapshot.symbol;
        current.start_time = bar_start;
    }
    
    if (current.tick_count == 0) {
        current.instrument_id = snapshot.instrument_id;
        current.symbol = snapshot.symbol;
        current.start_time = bar_start;
        start_time = bar_start;
    }
    
    update_bar(current, snapshot);
}

void BarBuilder::flush(BarCallback callback) {
    for (auto& [symbol, bar] : current_bars_) {
        if (bar.tick_count > 0 && callback) {
            callback(bar);
        }
    }
    current_bars_.clear();
    bar_start_times_.clear();
}

std::optional<Bar> BarBuilder::get_current(const std::string& symbol) const {
    auto it = current_bars_.find(symbol);
    if (it == current_bars_.end() || it->second.tick_count == 0) {
        return std::nullopt;
    }
    return it->second;
}

void BarBuilder::clear() {
    current_bars_.clear();
    bar_start_times_.clear();
}

} // namespace payoff::resampling
