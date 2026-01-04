/**
 * @file models.cpp
 * @brief Implementation of core data models
 */

#include "core/models.hpp"
#include <algorithm>
#include <sstream>

namespace payoff::core {

// ============================================================================
// DepthSnapshot Validation
// ============================================================================

void DepthSnapshot::validate() const {
    // Check instrument_id
    if (instrument_id == 0) {
        throw ValidationError("instrument_id cannot be 0");
    }
    
    // Check symbol
    if (symbol.empty()) {
        throw ValidationError("symbol cannot be empty");
    }
    
    // Validate bids are sorted DESC (highest first)
    for (size_t i = 1; i < bids.size(); ++i) {
        if (bids[i].price >= bids[i-1].price) {
            throw ValidationError(
                "bids must be sorted DESC (best bid first): found " +
                std::to_string(bids[i-1].price) + " before " + 
                std::to_string(bids[i].price));
        }
    }
    
    // Validate asks are sorted ASC (lowest first)
    for (size_t i = 1; i < asks.size(); ++i) {
        if (asks[i].price <= asks[i-1].price) {
            throw ValidationError(
                "asks must be sorted ASC (best ask first): found " +
                std::to_string(asks[i-1].price) + " before " + 
                std::to_string(asks[i].price));
        }
    }
    
    // Validate best_bid < best_ask (no crossed book)
    if (!bids.empty() && !asks.empty()) {
        if (bids.front().price >= asks.front().price) {
            throw ValidationError(
                "crossed book: best_bid (" + std::to_string(bids.front().price) +
                ") >= best_ask (" + std::to_string(asks.front().price) + ")");
        }
    }
    
    // Validate individual levels
    for (const auto& level : bids) {
        if (!level.is_valid()) {
            throw ValidationError("invalid bid level: price=" + 
                std::to_string(level.price) + " size=" + 
                std::to_string(level.size));
        }
    }
    for (const auto& level : asks) {
        if (!level.is_valid()) {
            throw ValidationError("invalid ask level: price=" + 
                std::to_string(level.price) + " size=" + 
                std::to_string(level.size));
        }
    }
}

bool DepthSnapshot::is_valid() const noexcept {
    try {
        // Use const_cast to call validate on const object
        const_cast<DepthSnapshot*>(this)->validate();
        return true;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// DepthSnapshotBuilder
// ============================================================================

DepthSnapshotBuilder& DepthSnapshotBuilder::instrument_id(uint32_t id) {
    snapshot_.instrument_id = id;
    return *this;
}

DepthSnapshotBuilder& DepthSnapshotBuilder::symbol(std::string sym) {
    snapshot_.symbol = std::move(sym);
    return *this;
}

DepthSnapshotBuilder& DepthSnapshotBuilder::exchange_timestamp(Timestamp ts) {
    snapshot_.exchange_timestamp = ts;
    return *this;
}

DepthSnapshotBuilder& DepthSnapshotBuilder::receive_timestamp(Timestamp ts) {
    snapshot_.receive_timestamp = ts;
    return *this;
}

DepthSnapshotBuilder& DepthSnapshotBuilder::add_bid(
    double price, int64_t size, int32_t orders) {
    snapshot_.bids.push_back({price, size, orders});
    return *this;
}

DepthSnapshotBuilder& DepthSnapshotBuilder::add_ask(
    double price, int64_t size, int32_t orders) {
    snapshot_.asks.push_back({price, size, orders});
    return *this;
}

DepthSnapshotBuilder& DepthSnapshotBuilder::trade_info(TradeInfo info) {
    snapshot_.trade = std::move(info);
    return *this;
}

DepthSnapshotBuilder& DepthSnapshotBuilder::source(Source src) {
    snapshot_.source = src;
    return *this;
}

DepthSnapshotBuilder& DepthSnapshotBuilder::partial(bool p) {
    snapshot_.is_partial = p;
    return *this;
}

DepthSnapshotBuilder& DepthSnapshotBuilder::stale(bool s) {
    snapshot_.is_stale = s;
    return *this;
}

DepthSnapshot DepthSnapshotBuilder::build() {
    // Sort bids DESC
    std::sort(snapshot_.bids.begin(), snapshot_.bids.end(),
        [](const DepthLevel& a, const DepthLevel& b) {
            return a.price > b.price;
        });
    
    // Sort asks ASC
    std::sort(snapshot_.asks.begin(), snapshot_.asks.end(),
        [](const DepthLevel& a, const DepthLevel& b) {
            return a.price < b.price;
        });
    
    snapshot_.validate();
    return std::move(snapshot_);
}

DepthSnapshot DepthSnapshotBuilder::build_unchecked() noexcept {
    return std::move(snapshot_);
}

} // namespace payoff::core
