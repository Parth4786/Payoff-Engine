/**
 * @file mock_source.cpp
 * @brief Mock data source for testing
 */

#include "core/datasource.hpp"
#include <random>

namespace payoff::core {

class MockSource : public MarketDataSource {
public:
    MockSource() : gen_(42) {}  // Fixed seed for determinism
    
    Source get_source_type() const noexcept override {
        return Source::Mock;
    }
    
    std::string get_name() const override {
        return "MockSource";
    }
    
    bool is_connected() const noexcept override {
        return true;
    }
    
    std::vector<std::string> get_symbols() const override {
        return {"NFO:49543", "NFO:49544", "NFO:49545"};
    }
    
    bool subscribe(const std::vector<std::string>& symbols) override {
        subscribed_ = symbols;
        return true;
    }
    
    void unsubscribe(const std::vector<std::string>& symbols) override {
        for (const auto& s : symbols) {
            subscribed_.erase(
                std::remove(subscribed_.begin(), subscribed_.end(), s),
                subscribed_.end());
        }
    }
    
    size_t replay_snapshots(
        const std::vector<std::string>& symbols,
        const TimeRange& range,
        SnapshotCallback callback) override {
        
        size_t count = 0;
        
        // Generate deterministic mock data
        auto current = range.start;
        auto step = std::chrono::milliseconds(100);
        
        while (current < range.end) {
            for (const auto& sym : (symbols.empty() ? get_symbols() : symbols)) {
                auto snapshot = generate_snapshot(sym, current);
                callback(snapshot);
                ++count;
            }
            current += step;
        }
        
        return count;
    }
    
    void start_streaming(
        SnapshotCallback callback,
        [[maybe_unused]] ErrorCallback error_callback) override {
        
        streaming_ = true;
        callback_ = callback;
        // In real implementation, would start async thread
    }
    
    void stop_streaming() override {
        streaming_ = false;
        callback_ = nullptr;
    }
    
    bool is_streaming() const noexcept override {
        return streaming_;
    }

private:
    std::mt19937 gen_;
    std::vector<std::string> subscribed_;
    bool streaming_ = false;
    SnapshotCallback callback_;
    
    DepthSnapshot generate_snapshot(const std::string& symbol, Timestamp ts) {
        std::uniform_real_distribution<> price_dist(100.0, 200.0);
        std::uniform_int_distribution<> size_dist(10, 1000);
        
        double base_price = price_dist(gen_);
        
        DepthSnapshotBuilder builder;
        builder.instrument_id(49543)
               .symbol(symbol)
               .exchange_timestamp(ts)
               .source(Source::Mock);
        
        // Add 5 bid levels
        for (int i = 0; i < 5; ++i) {
            builder.add_bid(
                base_price - 0.05 * (i + 1),
                size_dist(gen_),
                1 + i);
        }
        
        // Add 5 ask levels
        for (int i = 0; i < 5; ++i) {
            builder.add_ask(
                base_price + 0.05 * (i + 1),
                size_dist(gen_),
                1 + i);
        }
        
        TradeInfo trade;
        trade.last_price = base_price;
        trade.last_traded_quantity = size_dist(gen_);
        builder.trade_info(trade);
        
        return builder.build();
    }
};

std::unique_ptr<MarketDataSource> create_mock_source() {
    return std::make_unique<MockSource>();
}

} // namespace payoff::core
