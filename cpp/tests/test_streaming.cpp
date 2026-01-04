/**
 * @file test_streaming.cpp
 * @brief Unit tests for streaming pipeline
 */

#include "streaming/pipeline.hpp"
#include "core/models.hpp"
#include <cassert>
#include <iostream>

using namespace payoff::core;
using namespace payoff::streaming;

DepthSnapshot create_snapshot(const std::string& symbol, int64_t ts_ms) {
    DepthSnapshotBuilder builder;
    return builder
        .instrument_id(49543)
        .symbol(symbol)
        .exchange_timestamp(Timestamp{ts_ms})
        .add_bid(100.0, 500, 5)
        .add_ask(100.5, 400, 4)
        .source(Source::Mock)
        .build();
}

void test_dedup_duplicates() {
    std::cout << "Testing dedup - duplicates..." << std::endl;
    
    Deduplicator dedup;
    
    auto s1 = create_snapshot("NFO:49543", 1000);
    auto s2 = create_snapshot("NFO:49543", 1000);  // Same timestamp
    
    assert(dedup.process(s1) == true);
    assert(dedup.process(s2) == false);  // Duplicate dropped
    
    assert(dedup.stats().duplicates_dropped == 1);
    
    std::cout << "  ✓ Duplicate detection test passed" << std::endl;
}

void test_dedup_out_of_order() {
    std::cout << "Testing dedup - out of order..." << std::endl;
    
    Deduplicator dedup;
    
    auto s1 = create_snapshot("NFO:49543", 2000);
    auto s2 = create_snapshot("NFO:49543", 1500);  // Earlier timestamp
    
    assert(dedup.process(s1) == true);
    assert(dedup.process(s2) == false);  // Out of order dropped
    
    assert(dedup.stats().out_of_order_dropped == 1);
    
    std::cout << "  ✓ Out of order detection test passed" << std::endl;
}

void test_gap_detection() {
    std::cout << "Testing gap detection..." << std::endl;
    
    GapConfig config;
    config.gap_threshold = std::chrono::milliseconds(500);
    
    GapDetector detector(config);
    
    auto s1 = create_snapshot("NFO:49543", 1000);
    auto s2 = create_snapshot("NFO:49543", 2000);  // 1000ms gap > 500ms threshold
    
    Timestamp t1{1000};
    Timestamp t2{2000};
    
    detector.check_and_mark(s1, t1);
    bool stale = detector.check_and_mark(s2, t2);
    
    assert(stale == true);  // Should detect gap
    assert(s2.is_stale == true);
    
    std::cout << "  ✓ Gap detection test passed" << std::endl;
}

void test_assembler_full_update() {
    std::cout << "Testing assembler - full update..." << std::endl;
    
    Assembler assembler;
    
    auto snapshot = create_snapshot("NFO:49543", 1000);
    snapshot.is_partial = false;
    
    auto result = assembler.process(snapshot);
    
    assert(result.has_value());
    assert(result->symbol == "NFO:49543");
    
    std::cout << "  ✓ Full update passthrough test passed" << std::endl;
}

void test_assembler_partial_merge() {
    std::cout << "Testing assembler - partial merge..." << std::endl;
    
    Assembler assembler;
    
    // First partial - bids only
    DepthSnapshotBuilder builder1;
    auto partial1 = builder1
        .instrument_id(49543)
        .symbol("NFO:49543")
        .exchange_timestamp(Timestamp{1000})
        .add_bid(100.0, 500, 5)
        .source(Source::Mock)
        .partial(true)
        .build_unchecked();
    
    auto result1 = assembler.process(partial1);
    assert(!result1.has_value());  // Need both sides
    
    // Second partial - asks only
    DepthSnapshotBuilder builder2;
    auto partial2 = builder2
        .instrument_id(49543)
        .symbol("NFO:49543")
        .exchange_timestamp(Timestamp{1001})
        .add_ask(100.5, 400, 4)
        .source(Source::Mock)
        .partial(true)
        .build_unchecked();
    
    auto result2 = assembler.process(partial2);
    assert(result2.has_value());  // Now complete
    assert(!result2->bids.empty());
    assert(!result2->asks.empty());
    
    std::cout << "  ✓ Partial merge test passed" << std::endl;
}

void test_full_pipeline() {
    std::cout << "Testing full pipeline..." << std::endl;
    
    PipelineConfig config;
    config.gap_config.gap_threshold = std::chrono::milliseconds(500);
    
    StreamingPipeline pipeline(config);
    
    int output_count = 0;
    
    // Process some snapshots
    auto s1 = create_snapshot("NFO:49543", 1000);
    auto s2 = create_snapshot("NFO:49543", 1100);
    auto s3 = create_snapshot("NFO:49543", 1100);  // Duplicate
    auto s4 = create_snapshot("NFO:49543", 1200);
    
    Timestamp now{1200};
    
    pipeline.process(s1, now, [&](const DepthSnapshot&) { output_count++; });
    pipeline.process(s2, now, [&](const DepthSnapshot&) { output_count++; });
    pipeline.process(s3, now, [&](const DepthSnapshot&) { output_count++; });
    pipeline.process(s4, now, [&](const DepthSnapshot&) { output_count++; });
    
    assert(output_count == 3);  // s3 dropped as duplicate
    
    auto stats = pipeline.stats();
    assert(stats.input_count == 4);
    assert(stats.output_count == 3);
    
    std::cout << "  Input: " << stats.input_count << std::endl;
    std::cout << "  Output: " << stats.output_count << std::endl;
    std::cout << "  Dropped: " << stats.dropped_count << std::endl;
    std::cout << "  ✓ Full pipeline test passed" << std::endl;
}

int main() {
    std::cout << "\n=== Streaming Pipeline Unit Tests ===\n" << std::endl;
    
    test_dedup_duplicates();
    test_dedup_out_of_order();
    test_gap_detection();
    test_assembler_full_update();
    test_assembler_partial_merge();
    test_full_pipeline();
    
    std::cout << "\n✓ All streaming tests passed!\n" << std::endl;
    return 0;
}
