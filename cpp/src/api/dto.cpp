/**
 * @file dto.cpp
 * @brief Data Transfer Objects for API - JSON serialization helpers
 * 
 * Provides conversion utilities between internal models and JSON
 * for REST API responses.
 */

#include "payoff/models.hpp"
#include "core/models.hpp"
#include <sstream>
#include <iomanip>
#include <string>

namespace payoff::api {

// ============================================================================
// JSON Serialization Helpers
// ============================================================================

std::string escape_json_string(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 16);
    
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Control characters
                    std::ostringstream ss;
                    ss << "\\u" << std::hex << std::setfill('0') 
                       << std::setw(4) << static_cast<int>(c);
                    result += ss.str();
                } else {
                    result += c;
                }
        }
    }
    return result;
}

// ============================================================================
// Option Leg to JSON
// ============================================================================

std::string option_leg_to_json(const engine::OptionLeg& leg) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    
    ss << "{";
    ss << "\"type\":\"" << (leg.type == engine::OptionType::Call ? "CE" : "PE") << "\",";
    ss << "\"side\":\"" << (leg.side == engine::Side::Buy ? "BUY" : "SELL") << "\",";
    ss << "\"strike\":" << leg.strike << ",";
    ss << "\"quantity\":" << leg.quantity << ",";
    ss << "\"lot_size\":" << leg.lot_size << ",";
    ss << "\"premium\":" << leg.premium << ",";
    ss << "\"total_qty\":" << leg.total_quantity() << ",";
    ss << "\"net_premium\":" << leg.net_premium();
    
    if (!leg.symbol.empty()) {
        ss << ",\"symbol\":\"" << escape_json_string(leg.symbol) << "\"";
    }
    
    ss << "}";
    return ss.str();
}

// ============================================================================
// Strategy to JSON
// ============================================================================

std::string strategy_to_json(const engine::Strategy& strategy) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    
    ss << "{";
    ss << "\"name\":\"" << escape_json_string(strategy.name) << "\",";
    ss << "\"underlying\":\"" << escape_json_string(strategy.underlying) << "\",";
    ss << "\"underlying_price\":" << strategy.underlying_price << ",";
    ss << "\"total_premium\":" << strategy.total_premium() << ",";
    ss << "\"is_credit\":" << (strategy.is_credit() ? "true" : "false") << ",";
    ss << "\"leg_count\":" << strategy.leg_count() << ",";
    ss << "\"legs\":[";
    
    for (size_t i = 0; i < strategy.legs.size(); ++i) {
        if (i > 0) ss << ",";
        ss << option_leg_to_json(strategy.legs[i]);
    }
    
    ss << "]}";
    return ss.str();
}

// ============================================================================
// Greeks to JSON
// ============================================================================

std::string greeks_to_json(const engine::Greeks& greeks) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    
    ss << "{";
    ss << "\"delta\":" << greeks.delta << ",";
    ss << "\"gamma\":" << greeks.gamma << ",";
    ss << "\"theta\":" << greeks.theta << ",";
    ss << "\"vega\":" << greeks.vega << ",";
    ss << "\"rho\":" << greeks.rho;
    ss << "}";
    
    return ss.str();
}

// ============================================================================
// PayoffCurve to JSON
// ============================================================================

std::string payoff_curve_to_json(const engine::PayoffCurve& curve) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    
    ss << "{";
    ss << "\"scenario_name\":\"" << escape_json_string(curve.scenario_name) << "\",";
    ss << "\"max_profit\":" << curve.max_profit << ",";
    ss << "\"max_loss\":" << curve.max_loss << ",";
    
    // Breakevens
    ss << "\"breakevens\":[";
    for (size_t i = 0; i < curve.breakevens.size(); ++i) {
        if (i > 0) ss << ",";
        ss << curve.breakevens[i];
    }
    ss << "],";
    
    // Current Greeks
    ss << "\"greeks\":" << greeks_to_json(curve.current_greeks) << ",";
    
    // Points
    ss << "\"points\":[";
    for (size_t i = 0; i < curve.points.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{\"spot\":" << curve.points[i].spot 
           << ",\"pnl\":" << curve.points[i].pnl << "}";
    }
    ss << "]}";
    
    return ss.str();
}

// ============================================================================
// DepthSnapshot to JSON
// ============================================================================

std::string depth_snapshot_to_json(const core::DepthSnapshot& snap) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    
    ss << "{";
    ss << "\"instrument_id\":" << snap.instrument_id << ",";
    ss << "\"symbol\":\"" << escape_json_string(snap.symbol) << "\",";
    ss << "\"exchange_timestamp\":" << snap.exchange_timestamp.count() << ",";
    ss << "\"receive_timestamp\":" << snap.receive_timestamp.count() << ",";
    
    // Trade info
    ss << "\"last_price\":" << snap.trade.last_price << ",";
    ss << "\"volume\":" << snap.trade.total_traded_quantity << ",";
    ss << "\"oi\":" << snap.trade.open_interest << ",";
    ss << "\"open\":" << snap.trade.open << ",";
    ss << "\"high\":" << snap.trade.high << ",";
    ss << "\"low\":" << snap.trade.low << ",";
    ss << "\"close\":" << snap.trade.close << ",";
    
    // Best bid/ask
    ss << "\"best_bid\":" << (snap.best_bid().value_or(0.0)) << ",";
    ss << "\"best_ask\":" << (snap.best_ask().value_or(0.0)) << ",";
    ss << "\"midprice\":" << (snap.midprice().value_or(0.0)) << ",";
    ss << "\"spread\":" << (snap.spread().value_or(0.0)) << ",";
    
    // Bids
    ss << "\"bids\":[";
    for (size_t i = 0; i < snap.bids.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{\"price\":" << snap.bids[i].price 
           << ",\"size\":" << snap.bids[i].size 
           << ",\"orders\":" << snap.bids[i].orders << "}";
    }
    ss << "],";
    
    // Asks
    ss << "\"asks\":[";
    for (size_t i = 0; i < snap.asks.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{\"price\":" << snap.asks[i].price 
           << ",\"size\":" << snap.asks[i].size 
           << ",\"orders\":" << snap.asks[i].orders << "}";
    }
    ss << "],";
    
    ss << "\"source\":\"" << core::source_to_string(snap.source) << "\",";
    ss << "\"is_partial\":" << (snap.is_partial ? "true" : "false") << ",";
    ss << "\"is_stale\":" << (snap.is_stale ? "true" : "false");
    ss << "}";
    
    return ss.str();
}

} // namespace payoff::api
