/**
 * @file pipeline.cpp
 * @brief Streaming pipeline implementation
 */

#include "streaming/pipeline.hpp"

namespace payoff::streaming {

StreamingPipeline::StreamingPipeline(PipelineConfig config)
    : config_(std::move(config))
    , gap_detector_(config_.gap_config) {}

bool StreamingPipeline::process(
    const DepthSnapshot& snapshot,
    Timestamp current_time,
    OutputCallback callback) {
    
    input_count_++;
    
    DepthSnapshot working = snapshot;
    
    // Stage 1: Deduplication
    if (config_.enable_dedup) {
        if (!dedup_.process(working)) {
            return false;  // Dropped by dedup
        }
    }
    
    // Stage 2: Gap detection
    if (config_.enable_gap_detection) {
        [[maybe_unused]] bool is_stale = gap_detector_.check_and_mark(working, current_time);
    }
    
    // Stage 3: Assembly (for partial updates)
    if (config_.enable_assembly && working.is_partial) {
        auto complete = assembler_.process(working);
        if (!complete) {
            return false;  // Waiting for complete book
        }
        working = std::move(*complete);
    }
    
    // Output
    output_count_++;
    
    if (callback) {
        callback(working);
    }
    
    return true;
}

std::optional<DepthSnapshot> StreamingPipeline::process(
    const DepthSnapshot& snapshot,
    Timestamp current_time) {
    
    std::optional<DepthSnapshot> result;
    
    process(snapshot, current_time, [&result](const DepthSnapshot& s) {
        result = s;
    });
    
    return result;
}

PipelineStats StreamingPipeline::stats() const {
    PipelineStats s;
    s.input_count = input_count_.load();
    s.output_count = output_count_.load();
    s.dropped_count = s.input_count - s.output_count;
    s.dedup_stats = dedup_.stats().snapshot();
    s.gap_stats = gap_detector_.stats().snapshot();
    s.stale_count = s.gap_stats.stale_marked;
    return s;
}

void StreamingPipeline::reset_stats() {
    input_count_ = 0;
    output_count_ = 0;
    dedup_.reset_stats();
    gap_detector_.reset_stats();
}

void StreamingPipeline::clear() {
    assembler_.clear_all();
    dedup_.clear();
    gap_detector_.clear();
    reset_stats();
}

void StreamingPipeline::set_config(PipelineConfig config) {
    config_ = std::move(config);
    gap_detector_.set_config(config_.gap_config);
}

} // namespace payoff::streaming
