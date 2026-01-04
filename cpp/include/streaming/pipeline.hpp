#pragma once
/**
 * @file pipeline.hpp
 * @brief Complete streaming pipeline: Assembler → Dedup → GapDetector
 * 
 * Combines all streaming components into a single pipeline.
 * Ensures consistent processing order for both live and replay.
 */

#include "streaming/assembler.hpp"
#include "streaming/dedup.hpp"
#include "streaming/gap_detector.hpp"
#include <functional>
#include <memory>

namespace payoff::streaming {

// ============================================================================
// Pipeline Configuration
// ============================================================================
struct PipelineConfig {
    GapConfig gap_config;
    bool enable_dedup = true;
    bool enable_gap_detection = true;
    bool enable_assembly = true;
};

// ============================================================================
// Pipeline Statistics (copyable)
// ============================================================================
struct PipelineStats {
    uint64_t input_count = 0;
    uint64_t output_count = 0;
    uint64_t dropped_count = 0;
    uint64_t stale_count = 0;
    uint64_t partial_count = 0;
    
    DedupStatsSnapshot dedup_stats;
    GapStatsSnapshot gap_stats;
};

// ============================================================================
// StreamingPipeline
// ============================================================================
class StreamingPipeline {
public:
    using OutputCallback = std::function<void(const DepthSnapshot&)>;
    
    explicit StreamingPipeline(PipelineConfig config = {});
    
    /**
     * @brief Process a snapshot through the pipeline
     * @param snapshot Input snapshot
     * @param current_time Current market time
     * @param callback Called with processed snapshot if it passes all stages
     * @return true if snapshot passed through, false if dropped
     */
    bool process(
        const DepthSnapshot& snapshot,
        Timestamp current_time,
        OutputCallback callback);
    
    /**
     * @brief Process without callback (just check if would pass)
     */
    [[nodiscard]] std::optional<DepthSnapshot> process(
        const DepthSnapshot& snapshot,
        Timestamp current_time);
    
    /**
     * @brief Get pipeline statistics
     */
    [[nodiscard]] PipelineStats stats() const;
    
    /**
     * @brief Reset statistics
     */
    void reset_stats();
    
    /**
     * @brief Clear all state
     */
    void clear();
    
    /**
     * @brief Access individual components
     */
    [[nodiscard]] Assembler& assembler() { return assembler_; }
    [[nodiscard]] Deduplicator& deduplicator() { return dedup_; }
    [[nodiscard]] GapDetector& gap_detector() { return gap_detector_; }
    
    /**
     * @brief Update configuration
     */
    void set_config(PipelineConfig config);
    [[nodiscard]] const PipelineConfig& config() const { return config_; }

private:
    PipelineConfig config_;
    Assembler assembler_;
    Deduplicator dedup_;
    GapDetector gap_detector_;
    
    std::atomic<uint64_t> input_count_{0};
    std::atomic<uint64_t> output_count_{0};
};

} // namespace payoff::streaming
