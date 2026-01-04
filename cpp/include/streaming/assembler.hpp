#pragma once
/**
 * @file assembler.hpp
 * @brief Depth book assembler for partial updates
 * 
 * Merges partial updates (only bids OR only asks) into complete book.
 * Maintains per-symbol book state.
 * 
 * Reference: docs/Market-observatory/streaming/assembler.py
 */

#include "core/models.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace payoff::streaming {

using namespace payoff::core;

// ============================================================================
// BookState - Per-symbol order book state
// ============================================================================
struct BookState {
    std::vector<DepthLevel> bids;
    std::vector<DepthLevel> asks;
    TradeInfo trade;
    Timestamp last_update{0};
    bool has_bids = false;
    bool has_asks = false;
    
    [[nodiscard]] bool is_complete() const noexcept {
        return has_bids && has_asks;
    }
    
    void clear() noexcept {
        bids.clear();
        asks.clear();
        has_bids = false;
        has_asks = false;
    }
};

// ============================================================================
// Assembler - Merges partial updates into full books
// ============================================================================
class Assembler {
public:
    Assembler() = default;
    
    /**
     * @brief Process incoming snapshot
     * @param snapshot Incoming (possibly partial) snapshot
     * @return Complete snapshot if book is now complete, nullopt otherwise
     * 
     * If snapshot.is_partial:
     *   - Merges with existing book state
     *   - Returns complete snapshot when both sides received
     * If !snapshot.is_partial:
     *   - Replaces book state entirely
     *   - Returns the snapshot immediately
     */
    [[nodiscard]] std::optional<DepthSnapshot> process(const DepthSnapshot& snapshot);
    
    /**
     * @brief Get current book state for a symbol
     */
    [[nodiscard]] const BookState* get_book(const std::string& symbol) const;
    
    /**
     * @brief Clear state for a symbol
     */
    void clear_symbol(const std::string& symbol);
    
    /**
     * @brief Clear all state
     */
    void clear_all();
    
    /**
     * @brief Get number of tracked symbols
     */
    [[nodiscard]] size_t symbol_count() const noexcept;

private:
    std::unordered_map<std::string, BookState> books_;
    mutable std::mutex mutex_;
    
    DepthSnapshot build_complete_snapshot(
        const DepthSnapshot& update,
        const BookState& state) const;
};

} // namespace payoff::streaming
