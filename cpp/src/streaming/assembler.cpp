/**
 * @file assembler.cpp
 * @brief Depth book assembler implementation
 */

#include "streaming/assembler.hpp"
#include <algorithm>

namespace payoff::streaming {

std::optional<DepthSnapshot> Assembler::process(const DepthSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& state = books_[snapshot.symbol];
    
    // If not partial, this is a complete update
    if (!snapshot.is_partial) {
        state.bids = snapshot.bids;
        state.asks = snapshot.asks;
        state.trade = snapshot.trade;
        state.last_update = snapshot.exchange_timestamp;
        state.has_bids = !snapshot.bids.empty();
        state.has_asks = !snapshot.asks.empty();
        return snapshot;  // Pass through immediately
    }
    
    // Partial update - merge with existing state
    if (!snapshot.bids.empty()) {
        state.bids = snapshot.bids;
        state.has_bids = true;
    }
    
    if (!snapshot.asks.empty()) {
        state.asks = snapshot.asks;
        state.has_asks = true;
    }
    
    // Always update trade info if present
    if (snapshot.trade.has_trade()) {
        state.trade = snapshot.trade;
    }
    
    state.last_update = snapshot.exchange_timestamp;
    
    // Check if we now have a complete book
    if (state.is_complete()) {
        return build_complete_snapshot(snapshot, state);
    }
    
    return std::nullopt;  // Still waiting for other side
}

DepthSnapshot Assembler::build_complete_snapshot(
    const DepthSnapshot& update,
    const BookState& state) const {
    
    DepthSnapshot complete;
    complete.instrument_id = update.instrument_id;
    complete.symbol = update.symbol;
    complete.exchange_timestamp = update.exchange_timestamp;
    complete.receive_timestamp = update.receive_timestamp;
    complete.bids = state.bids;
    complete.asks = state.asks;
    complete.trade = state.trade;
    complete.source = update.source;
    complete.is_partial = false;
    complete.is_stale = update.is_stale;
    
    return complete;
}

const BookState* Assembler::get_book(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = books_.find(symbol);
    return it != books_.end() ? &it->second : nullptr;
}

void Assembler::clear_symbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    books_.erase(symbol);
}

void Assembler::clear_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    books_.clear();
}

size_t Assembler::symbol_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return books_.size();
}

} // namespace payoff::streaming
