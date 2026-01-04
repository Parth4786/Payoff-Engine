/**
 * @file test_features.cpp
 * @brief Unit tests for features engine (determinism)
 */

#include "features/engine.hpp"
#include "core/models.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace payoff::core;
using namespace payoff::features;

// Helper to compare doubles
bool approx_equal(double a, double b, double epsilon = 0.0001) {
    return std::abs(a - b) < epsilon;
}

DepthSnapshot create_test_snapshot() {
    DepthSnapshotBuilder builder;
    return builder
        .instrument_id(49543)
        .symbol("NFO:49543")
        .exchange_timestamp(Timestamp{1704369600000})
        .add_bid(100.0, 500, 5)
        .add_bid(99.5, 600, 6)
        .add_bid(99.0, 700, 7)
        .add_ask(100.5, 400, 4)
        .add_ask(101.0, 500, 5)
        .add_ask(101.5, 600, 6)
        .source(Source::Mock)
        .build();
}

void test_midprice() {
    std::cout << "Testing midprice calculation..." << std::endl;
    
    auto snapshot = create_test_snapshot();
    auto mid = compute_midprice(snapshot);
    
    assert(mid.has_value());
    assert(approx_equal(mid.value(), 100.25));  // (100.0 + 100.5) / 2
    
    std::cout << "  ✓ Midprice test passed" << std::endl;
}

void test_spread() {
    std::cout << "Testing spread calculation..." << std::endl;
    
    auto snapshot = create_test_snapshot();
    auto spread = compute_spread(snapshot);
    
    assert(spread.has_value());
    assert(approx_equal(spread.value(), 0.5));  // 100.5 - 100.0
    
    std::cout << "  ✓ Spread test passed" << std::endl;
}

void test_imbalance() {
    std::cout << "Testing imbalance calculation..." << std::endl;
    
    auto snapshot = create_test_snapshot();
    double imbalance = compute_imbalance(snapshot);
    
    // (500 - 400) / (500 + 400) = 100/900 ≈ 0.111
    assert(approx_equal(imbalance, 0.1111, 0.001));
    
    std::cout << "  ✓ Imbalance test passed" << std::endl;
}

void test_microprice() {
    std::cout << "Testing microprice calculation..." << std::endl;
    
    auto snapshot = create_test_snapshot();
    auto micro = compute_microprice(snapshot);
    
    assert(micro.has_value());
    // microprice = (bid * ask_size + ask * bid_size) / (bid_size + ask_size)
    // = (100.0 * 400 + 100.5 * 500) / 900
    // = (40000 + 50250) / 900 = 100.2778
    assert(approx_equal(micro.value(), 100.2778, 0.001));
    
    std::cout << "  ✓ Microprice test passed" << std::endl;
}

void test_determinism() {
    std::cout << "Testing determinism..." << std::endl;
    
    auto snapshot = create_test_snapshot();
    
    // Compute features multiple times
    auto f1 = compute_features(snapshot);
    auto f2 = compute_features(snapshot);
    auto f3 = compute_features(snapshot);
    
    // All results must be identical
    assert(f1.midprice == f2.midprice);
    assert(f2.midprice == f3.midprice);
    assert(f1.spread == f2.spread);
    assert(f2.spread == f3.spread);
    assert(f1.bid_ask_imbalance == f2.bid_ask_imbalance);
    assert(f2.bid_ask_imbalance == f3.bid_ask_imbalance);
    
    std::cout << "  ✓ Determinism test passed" << std::endl;
}

void test_ofi() {
    std::cout << "Testing Order Flow Imbalance..." << std::endl;
    
    auto snapshot1 = create_test_snapshot();
    
    // Create second snapshot with changed sizes
    DepthSnapshotBuilder builder;
    auto snapshot2 = builder
        .instrument_id(49543)
        .symbol("NFO:49543")
        .exchange_timestamp(Timestamp{1704369600100})
        .add_bid(100.0, 600, 5)  // +100
        .add_bid(99.5, 600, 6)
        .add_ask(100.5, 350, 4)  // -50
        .add_ask(101.0, 500, 5)
        .source(Source::Mock)
        .build();
    
    double ofi = compute_ofi(snapshot2, snapshot1);
    
    // OFI = bid_change - ask_change = 100 - (-50) = 150
    assert(approx_equal(ofi, 150.0));
    
    std::cout << "  ✓ OFI test passed" << std::endl;
}

int main() {
    std::cout << "\n=== Features Engine Unit Tests ===\n" << std::endl;
    
    test_midprice();
    test_spread();
    test_imbalance();
    test_microprice();
    test_determinism();
    test_ofi();
    
    std::cout << "\n✓ All features tests passed!\n" << std::endl;
    return 0;
}
