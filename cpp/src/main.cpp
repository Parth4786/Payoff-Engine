/**
 * @file main.cpp
 * @brief Payoff Engine Entry Point
 */

#include <iostream>
#include <string>

#include "core/config.hpp"
#include "core/models.hpp"
#include "core/instrument_manager.hpp"
#include "core/market_clock.hpp"
#include "core/datasource.hpp"
#include "core/replay_engine.hpp"
#include "kite/kite_client.hpp"
#include "payoff/models.hpp"
#include "payoff/pricing.hpp"
#include "payoff/calculator.hpp"
#include "features/engine.hpp"
#include "execution/hint_engine.hpp"
#include "streaming/pipeline.hpp"
#include "cache/market_cache.hpp"

using namespace payoff;

void print_banner() {
    std::cout << R"(
╔═══════════════════════════════════════════════════════════════╗
║                    PAYOFF ENGINE v1.0.0                       ║
║        Production-Grade Options Analytics Platform            ║
╚═══════════════════════════════════════════════════════════════╝
)" << std::endl;
}

void demo_payoff_calculation() {
    std::cout << "\n=== Payoff Engine Demo ===\n" << std::endl;
    
    // Create a simple Bull Call Spread
    engine::Strategy strategy;
    strategy.name = "Bull Call Spread";
    strategy.underlying = "NIFTY";
    strategy.underlying_price = 23000.0;
    
    // Buy ATM Call
    engine::OptionLeg long_call;
    long_call.type = engine::OptionType::Call;
    long_call.side = engine::Side::Buy;
    long_call.strike = 23000.0;
    long_call.quantity = 1;
    long_call.lot_size = 25;
    long_call.premium = 250.0;
    strategy.legs.push_back(long_call);
    
    // Sell OTM Call
    engine::OptionLeg short_call;
    short_call.type = engine::OptionType::Call;
    short_call.side = engine::Side::Sell;
    short_call.strike = 23500.0;
    short_call.quantity = 1;
    short_call.lot_size = 25;
    short_call.premium = 100.0;
    strategy.legs.push_back(short_call);
    
    std::cout << "Strategy: " << strategy.name << std::endl;
    std::cout << "Underlying: " << strategy.underlying 
              << " @ " << strategy.underlying_price << std::endl;
    std::cout << "Net Premium: " << strategy.total_premium() << std::endl;
    std::cout << "Type: " << (strategy.is_debit() ? "Debit" : "Credit") << std::endl;
    
    // Calculate payoff
    engine::PayoffCalculator calculator;
    auto curve = calculator.calculate_expiry_payoff(strategy);
    
    std::cout << "\n--- At Expiry ---" << std::endl;
    std::cout << "Max Profit: " << curve.max_profit << std::endl;
    std::cout << "Max Loss: " << curve.max_loss << std::endl;
    std::cout << "Breakevens: ";
    for (double be : curve.breakevens) {
        std::cout << be << " ";
    }
    std::cout << std::endl;
    
    // Calculate Greeks
    auto greeks = calculator.calculate_strategy_greeks(strategy, 0.20, 30);
    std::cout << "\n--- Greeks (30 DTE, 20% IV) ---" << std::endl;
    std::cout << "Delta: " << greeks.delta << std::endl;
    std::cout << "Gamma: " << greeks.gamma << std::endl;
    std::cout << "Theta: " << greeks.theta << " (per day)" << std::endl;
    std::cout << "Vega: " << greeks.vega << " (per 1% IV)" << std::endl;
}

void demo_black_scholes() {
    std::cout << "\n=== Black-Scholes Pricing Demo ===\n" << std::endl;
    
    engine::PricingParams params;
    params.spot = 23000.0;
    params.strike = 23000.0;
    params.time_to_expiry = 30.0 / 365.0;  // 30 days
    params.volatility = 0.20;  // 20% IV
    params.risk_free_rate = 0.07;
    
    double call_price = engine::bs_call_price(params);
    double put_price = engine::bs_put_price(params);
    
    std::cout << "ATM Call Price: " << call_price << std::endl;
    std::cout << "ATM Put Price: " << put_price << std::endl;
    
    auto call_greeks = engine::calculate_greeks(params, engine::OptionType::Call);
    std::cout << "\nCall Greeks:" << std::endl;
    std::cout << "  Delta: " << call_greeks.delta << std::endl;
    std::cout << "  Gamma: " << call_greeks.gamma << std::endl;
    std::cout << "  Theta: " << call_greeks.theta << std::endl;
    std::cout << "  Vega: " << call_greeks.vega << std::endl;
}

void demo_features() {
    std::cout << "\n=== Market Features Demo ===\n" << std::endl;
    
    // Create a mock snapshot
    core::DepthSnapshot snapshot;
    snapshot.instrument_id = 49543;
    snapshot.symbol = "NFO:49543";
    snapshot.exchange_timestamp = core::Timestamp{1704369600000};
    
    // Add bids (DESC order)
    snapshot.bids = {
        {23000.0, 500, 10},
        {22995.0, 750, 15},
        {22990.0, 1000, 20},
        {22985.0, 1200, 25},
        {22980.0, 1500, 30}
    };
    
    // Add asks (ASC order)
    snapshot.asks = {
        {23005.0, 400, 8},
        {23010.0, 600, 12},
        {23015.0, 800, 16},
        {23020.0, 1000, 20},
        {23025.0, 1200, 24}
    };
    
    snapshot.source = core::Source::Mock;
    
    // Compute features
    auto features_result = features::compute_features(snapshot);
    
    std::cout << "Symbol: " << features_result.symbol << std::endl;
    std::cout << "Midprice: " << features_result.midprice << std::endl;
    std::cout << "Spread: " << features_result.spread << std::endl;
    std::cout << "Spread (bps): " << features_result.spread_bps << std::endl;
    std::cout << "Bid-Ask Imbalance: " << features_result.bid_ask_imbalance << std::endl;
    std::cout << "Microprice: " << features_result.microprice << std::endl;
    std::cout << "Depth Slope: " << features_result.depth_slope << std::endl;
    
    // Generate execution hint
    execution::HintEngine hint_engine;
    auto hint = hint_engine.generate_hint(features_result, 1);
    
    std::cout << "\n--- Execution Hint ---" << std::endl;
    std::cout << "Posture: " << execution::posture_to_string(hint.posture) << std::endl;
    std::cout << "Confidence: " << (hint.confidence * 100) << "%" << std::endl;
    std::cout << "Reasons:" << std::endl;
    for (const auto& reason : hint.reasons) {
        std::cout << "  - " << reason << std::endl;
    }
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    print_banner();
    
    std::cout << "Payoff Engine initialized successfully.\n" << std::endl;
    std::cout << "Components:" << std::endl;
    std::cout << "  ✓ Core Models" << std::endl;
    std::cout << "  ✓ Instrument Manager" << std::endl;
    std::cout << "  ✓ Market Clock" << std::endl;
    std::cout << "  ✓ Black-Scholes Pricing" << std::endl;
    std::cout << "  ✓ Payoff Calculator" << std::endl;
    std::cout << "  ✓ Features Engine" << std::endl;
    std::cout << "  ✓ Execution Hint Engine" << std::endl;
    std::cout << "  ✓ Streaming Pipeline" << std::endl;
    std::cout << "  ✓ Market Cache" << std::endl;
    
    // Run demos
    demo_black_scholes();
    demo_payoff_calculation();
    demo_features();
    
    std::cout << "\n=== Ready for Production ===\n" << std::endl;
    
    // Test connectivity
    std::cout << "=== Testing Connectivity ===\n" << std::endl;
    
    // Load config
    auto& cfg = config::config();
    cfg.load();
    
    // Test ClickHouse
    std::cout << "Testing ClickHouse connection..." << std::endl;
    try {
        auto ch_source = core::create_clickhouse_source_from_config();
        if (ch_source && ch_source->is_connected()) {
            std::cout << "  ✓ ClickHouse: Connected to " << cfg.ch_host() 
                      << ":" << cfg.ch_port() << "/" << cfg.ch_database() << std::endl;
            
            // Get available symbols
            auto symbols = ch_source->get_symbols();
            std::cout << "  ✓ Available instruments: " << symbols.size() << std::endl;
        } else {
            std::cout << "  ✗ ClickHouse: Connection failed" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "  ✗ ClickHouse: " << e.what() << std::endl;
    }
    
    // Test Kite (if access token available)
    std::cout << "\nTesting Kite connection..." << std::endl;
    try {
        auto kite_client = kite::create_kite_client();
        if (kite_client && !cfg.kite_access_token().empty()) {
            // Try to get profile
            auto [profile, error] = kite_client->profile();
            if (error == kite::KiteError::None) {
                std::cout << "  ✓ Kite: Connected as " << profile.user_id << std::endl;
            } else {
                std::cout << "  ✗ Kite: API call failed (token may be expired)" << std::endl;
                std::cout << "    Login URL: " << kite_client->login_url() << std::endl;
            }
        } else {
            std::cout << "  ! Kite: No access token configured" << std::endl;
            auto temp_client = kite::create_kite_client();
            if (temp_client) {
                std::cout << "    Login URL: " << temp_client->login_url() << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cout << "  ✗ Kite: " << e.what() << std::endl;
    }
    
    // Load instruments
    std::cout << "\nLoading instruments..." << std::endl;
    try {
        auto& inst_mgr = core::get_instrument_manager();
        std::string inst_dir = cfg.get("KITE_INSTRUMENT_MASTER_DIR", "");
        if (!inst_dir.empty()) {
            size_t count = inst_mgr.load_directory(inst_dir);
            std::cout << "  ✓ Loaded " << count << " instruments" << std::endl;
            std::cout << "  ✓ NSE: " << inst_mgr.count_by_exchange(core::Exchange::NSE) << std::endl;
            std::cout << "  ✓ NFO: " << inst_mgr.count_by_exchange(core::Exchange::NFO) << std::endl;
        } else {
            std::cout << "  ! No instrument directory configured" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "  ✗ Instruments: " << e.what() << std::endl;
    }
    
    std::cout << "\n=== System Ready ===\n" << std::endl;
    
    return 0;
}
