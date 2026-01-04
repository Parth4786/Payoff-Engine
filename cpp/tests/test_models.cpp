/**
 * @file test_models.cpp
 * @brief Unit tests for core models
 */

#include "core/models.hpp"
#include <cassert>
#include <iostream>

using namespace payoff::core;

void test_depth_level() {
    std::cout << "Testing DepthLevel..." << std::endl;
    
    DepthLevel level{100.0, 500, 5};
    assert(level.is_valid());
    assert(level.price == 100.0);
    assert(level.size == 500);
    assert(level.orders == 5);
    
    DepthLevel invalid{-1.0, 100, 1};
    assert(!invalid.is_valid());
    
    std::cout << "  ✓ DepthLevel tests passed" << std::endl;
}

void test_depth_snapshot_validation() {
    std::cout << "Testing DepthSnapshot validation..." << std::endl;
    
    // Valid snapshot
    DepthSnapshotBuilder builder;
    auto snapshot = builder
        .instrument_id(49543)
        .symbol("NFO:49543")
        .exchange_timestamp(Timestamp{1704369600000})
        .add_bid(100.0, 500, 5)
        .add_bid(99.5, 600, 6)
        .add_ask(100.5, 400, 4)
        .add_ask(101.0, 500, 5)
        .source(Source::Mock)
        .build();
    
    assert(snapshot.is_valid());
    assert(snapshot.instrument_id == 49543);
    assert(snapshot.bids.size() == 2);
    assert(snapshot.asks.size() == 2);
    
    // Check sorting invariants
    assert(snapshot.bids[0].price > snapshot.bids[1].price);  // DESC
    assert(snapshot.asks[0].price < snapshot.asks[1].price);  // ASC
    
    // Check no crossed book
    assert(snapshot.best_bid().value() < snapshot.best_ask().value());
    
    std::cout << "  ✓ DepthSnapshot validation tests passed" << std::endl;
}

void test_snapshot_accessors() {
    std::cout << "Testing DepthSnapshot accessors..." << std::endl;
    
    DepthSnapshotBuilder builder;
    auto snapshot = builder
        .instrument_id(49543)
        .symbol("NFO:49543")
        .add_bid(100.0, 500, 5)
        .add_ask(101.0, 400, 4)
        .source(Source::Mock)
        .build();
    
    assert(snapshot.best_bid().value() == 100.0);
    assert(snapshot.best_ask().value() == 101.0);
    assert(snapshot.midprice().value() == 100.5);
    assert(snapshot.spread().value() == 1.0);
    assert(snapshot.total_bid_size() == 500);
    assert(snapshot.total_ask_size() == 400);
    
    std::cout << "  ✓ DepthSnapshot accessor tests passed" << std::endl;
}

void test_crossed_book_rejected() {
    std::cout << "Testing crossed book rejection..." << std::endl;
    
    bool threw = false;
    try {
        DepthSnapshotBuilder builder;
        auto snapshot = builder
            .instrument_id(49543)
            .symbol("NFO:49543")
            .add_bid(101.0, 500, 5)  // Bid higher than ask!
            .add_ask(100.0, 400, 4)
            .source(Source::Mock)
            .build();
    } catch (const ValidationError& e) {
        threw = true;
        std::cout << "  Caught expected error: " << e.what() << std::endl;
    }
    
    assert(threw && "Crossed book should throw ValidationError");
    std::cout << "  ✓ Crossed book rejection test passed" << std::endl;
}

int main() {
    std::cout << "\n=== Core Models Unit Tests ===\n" << std::endl;
    
    test_depth_level();
    test_depth_snapshot_validation();
    test_snapshot_accessors();
    test_crossed_book_rejected();
    
    std::cout << "\n✓ All model tests passed!\n" << std::endl;
    return 0;
}
