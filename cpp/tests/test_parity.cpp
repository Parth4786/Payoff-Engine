/**
 * @file test_parity.cpp
 * @brief Parity tests - verify replay and stream produce same outputs
 */

#include "core/datasource.hpp"
#include "streaming/pipeline.hpp"
#include "features/engine.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace payoff::core;
using namespace payoff::streaming;
using namespace payoff::features;

void test_replay_stream_parity() {
    std::cout << "Testing replay vs stream parity..." << std::endl;
    
    // Create mock source
    auto source = create_mock_source();
    
    // Collect snapshots from replay
    std::vector<DepthSnapshot> replay_snapshots;
    TimeRange range{Timestamp{1000}, Timestamp{2000}};
    
    source->replay_snapshots({}, range, [&](const DepthSnapshot& snap) {
        replay_snapshots.push_back(snap);
    });
    
    // Process through pipeline (simulating streaming)
    PipelineConfig config;
    StreamingPipeline pipeline(config);
    
    std::vector<DepthSnapshot> stream_results;
    for (const auto& snap : replay_snapshots) {
        pipeline.process(snap, snap.exchange_timestamp, 
            [&](const DepthSnapshot& processed) {
                stream_results.push_back(processed);
            });
    }
    
    // Compute features for both
    std::vector<FeatureSnapshot> replay_features;
    std::vector<FeatureSnapshot> stream_features;
    
    std::optional<DepthSnapshot> prev;
    for (const auto& snap : replay_snapshots) {
        replay_features.push_back(compute_features(snap, prev));
        prev = snap;
    }
    
    prev = std::nullopt;
    for (const auto& snap : stream_results) {
        stream_features.push_back(compute_features(snap, prev));
        prev = snap;
    }
    
    // Verify parity (features should be identical)
    // Note: Dedup may reduce stream_results count
    std::cout << "  Replay snapshots: " << replay_snapshots.size() << std::endl;
    std::cout << "  Stream results: " << stream_results.size() << std::endl;
    std::cout << "  Replay features: " << replay_features.size() << std::endl;
    std::cout << "  Stream features: " << stream_features.size() << std::endl;
    
    // For valid comparisons, check same-timestamp features match
    for (size_t i = 0; i < std::min(replay_features.size(), stream_features.size()); ++i) {
        if (replay_features[i].timestamp == stream_features[i].timestamp) {
            assert(replay_features[i].midprice == stream_features[i].midprice);
            assert(replay_features[i].spread == stream_features[i].spread);
        }
    }
    
    std::cout << "  ✓ Replay vs stream parity test passed" << std::endl;
}

void test_deterministic_replay() {
    std::cout << "Testing deterministic replay..." << std::endl;
    
    auto source = create_mock_source();
    TimeRange range{Timestamp{1000}, Timestamp{1500}};
    
    // Run replay twice
    std::vector<DepthSnapshot> run1, run2;
    
    source->replay_snapshots({}, range, [&](const DepthSnapshot& snap) {
        run1.push_back(snap);
    });
    
    source->replay_snapshots({}, range, [&](const DepthSnapshot& snap) {
        run2.push_back(snap);
    });
    
    // Both runs should produce identical results
    assert(run1.size() == run2.size());
    
    for (size_t i = 0; i < run1.size(); ++i) {
        assert(run1[i].instrument_id == run2[i].instrument_id);
        assert(run1[i].symbol == run2[i].symbol);
        assert(run1[i].exchange_timestamp == run2[i].exchange_timestamp);
        // Note: Mock source uses fixed seed, so prices should match too
    }
    
    std::cout << "  Replay count: " << run1.size() << std::endl;
    std::cout << "  ✓ Deterministic replay test passed" << std::endl;
}

void test_no_future_leakage() {
    std::cout << "Testing no future data leakage..." << std::endl;
    
    // In replay mode, each snapshot should only see past data
    auto source = create_mock_source();
    TimeRange range{Timestamp{1000}, Timestamp{2000}};
    
    Timestamp last_seen{0};
    bool leaked = false;
    
    source->replay_snapshots({}, range, [&](const DepthSnapshot& snap) {
        // Each snapshot timestamp must be >= last seen
        if (snap.exchange_timestamp < last_seen) {
            leaked = true;
        }
        last_seen = snap.exchange_timestamp;
    });
    
    assert(!leaked && "Detected future data leakage in replay");
    
    std::cout << "  ✓ No future leakage test passed" << std::endl;
}

int main() {
    std::cout << "\n=== Parity Tests ===\n" << std::endl;
    
    test_deterministic_replay();
    test_no_future_leakage();
    test_replay_stream_parity();
    
    std::cout << "\n✓ All parity tests passed!\n" << std::endl;
    return 0;
}
