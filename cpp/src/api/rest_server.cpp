/**
 * @file rest_server.cpp
 * @brief REST API server implementation
 * 
 * Endpoints:
 * - GET  /api/health              - Health check
 * - GET  /api/config              - Get configuration
 * - POST /api/payoff/calculate    - Calculate payoff curve
 * - POST /api/greeks/calculate    - Calculate Greeks
 * - POST /api/strategy/analyze    - Full strategy analysis
 * - GET  /api/instruments         - List instruments
 * - GET  /api/quote/:symbol       - Get quote for symbol
 * - POST /api/sensitivity         - Generate sensitivity surface
 */

#include "api/http_server.hpp"
#include "api/websocket_server.hpp"
#include "core/config.hpp"
#include "core/models.hpp"
#include "core/instrument_manager.hpp"
#include "core/clickhouse_http.hpp"
#include "cache/market_cache.hpp"
#include "payoff/calculator.hpp"
#include "payoff/pricing.hpp"
#include "payoff/models.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cctype>
#include <vector>

namespace payoff::api {

// ============================================================================
// JSON Helpers
// ============================================================================

namespace {

// Simple JSON builder (for demo - use nlohmann::json in production)
class JsonBuilder {
public:
    JsonBuilder& start_object() {
        mark_value_written_();
        ss_ << "{";
        first_stack_.push_back(true);
        return *this;
    }
    
    JsonBuilder& end_object() {
        ss_ << "}";
        if (first_stack_.size() > 1) first_stack_.pop_back();
        return *this;
    }
    
    JsonBuilder& start_array() {
        mark_value_written_();
        ss_ << "[";
        first_stack_.push_back(true);
        return *this;
    }
    
    JsonBuilder& end_array() {
        ss_ << "]";
        if (first_stack_.size() > 1) first_stack_.pop_back();
        return *this;
    }
    
    JsonBuilder& key(const std::string& k) {
        if (!first_stack_.empty() && !first_stack_.back()) ss_ << ",";
        ss_ << "\"" << k << "\":";
        if (!first_stack_.empty()) first_stack_.back() = false;
        return *this;
    }
    
    JsonBuilder& value(const std::string& v) {
        ss_ << "\"" << escape(v) << "\"";
        mark_value_written_();
        return *this;
    }

    JsonBuilder& value(const char* v) {
        ss_ << "\"" << escape(v ? std::string(v) : std::string()) << "\"";
        mark_value_written_();
        return *this;
    }
    
    JsonBuilder& value(double v) {
        if (!std::isfinite(v)) {
            if (std::isinf(v)) {
                // JSON does not support Infinity, but large exponents are valid JSON numbers.
                // JSON.parse("1e309") yields Infinity in JS.
                ss_ << (v > 0 ? "1e309" : "-1e309");
                mark_value_written_();
                return *this;
            }
            // NaN: emit a safe numeric value
            ss_ << "0";
            mark_value_written_();
            return *this;
        }

        ss_ << std::fixed << std::setprecision(4) << v;
        mark_value_written_();
        return *this;
    }
    
    JsonBuilder& value(int v) {
        ss_ << v;
        mark_value_written_();
        return *this;
    }
    
    JsonBuilder& value(int64_t v) {
        ss_ << v;
        mark_value_written_();
        return *this;
    }
    
    JsonBuilder& value(bool v) {
        ss_ << (v ? "true" : "false");
        mark_value_written_();
        return *this;
    }
    
    JsonBuilder& raw(const std::string& r) {
        ss_ << r;
        mark_value_written_();
        return *this;
    }
    
    JsonBuilder& next() {
        ss_ << ",";
        if (!first_stack_.empty()) first_stack_.back() = false;
        return *this;
    }
    
    std::string str() const { return ss_.str(); }
    
private:
    std::ostringstream ss_;
    std::vector<bool> first_stack_{true};

    void mark_value_written_() {
        if (!first_stack_.empty()) first_stack_.back() = false;
    }
    
    static std::string escape(const std::string& s) {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
            }
        }
        return result;
    }
};

// Parse simple JSON value (for demo)
std::string json_get_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    
    pos += search.length();
    while (pos < json.length() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
    
    if (pos >= json.length() || json[pos] != '"') return "";
    pos++;
    
    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";
    
    return json.substr(pos, end - pos);
}

double json_get_double(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0.0;
    
    pos += search.length();
    while (pos < json.length() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
    
    auto end = json.find_first_of(",}]", pos);
    if (end == std::string::npos) return 0.0;
    
    try {
        return std::stod(json.substr(pos, end - pos));
    } catch (...) {
        return 0.0;
    }
}

int json_get_int(const std::string& json, const std::string& key) {
    return static_cast<int>(json_get_double(json, key));
}

static std::string extract_object_after_key(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
    if (pos >= json.size() || json[pos] != '{') return "";

    size_t i = pos;
    int depth = 0;
    bool in_string = false;
    bool escape = false;

    for (; i < json.size(); ++i) {
        const char c = json[i];
        if (in_string) {
            if (escape) {
                escape = false;
                continue;
            }
            if (c == '\\') {
                escape = true;
                continue;
            }
            if (c == '"') in_string = false;
            continue;
        }

        if (c == '"') {
            in_string = true;
            continue;
        }
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) {
                return json.substr(pos, i - pos + 1);
            }
        }
    }
    return "";
}

static std::vector<std::string> parse_string_array_after_key(const std::string& json, const std::string& key) {
    std::vector<std::string> items;
    const std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return items;
    pos += search.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
    if (pos >= json.size() || json[pos] != '[') return items;

    auto end = json.find(']', pos);
    if (end == std::string::npos) return items;
    const std::string arr = json.substr(pos, end - pos + 1);

    size_t i = 0;
    while ((i = arr.find('"', i)) != std::string::npos) {
        auto j = arr.find('"', i + 1);
        if (j == std::string::npos) break;
        items.push_back(arr.substr(i + 1, j - i - 1));
        i = j + 1;
    }
    return items;
}

static engine::Strategy parse_strategy_string(const std::string& body) {
    engine::Strategy strategy;

    strategy.underlying = json_get_string(body, "underlying");
    if (strategy.underlying.empty()) strategy.underlying = "NIFTY";

    strategy.name = json_get_string(body, "name");
    if (strategy.name.empty()) strategy.name = "Custom Strategy";

    double spot = json_get_double(body, "spot");
    if (spot <= 0) spot = json_get_double(body, "underlying_price");
    if (spot <= 0) spot = 26300.0;
    strategy.underlying_price = spot;

    auto legs_pos = body.find("\"legs\"");
    if (legs_pos != std::string::npos) {
        auto arr_start = body.find('[', legs_pos);
        auto arr_end = body.find(']', arr_start);
        if (arr_start != std::string::npos && arr_end != std::string::npos) {
            const std::string legs_str = body.substr(arr_start, arr_end - arr_start + 1);

            size_t pos = 0;
            while ((pos = legs_str.find('{', pos)) != std::string::npos) {
                auto leg_end = legs_str.find('}', pos);
                if (leg_end == std::string::npos) break;

                const std::string leg_json = legs_str.substr(pos, leg_end - pos + 1);
                engine::OptionLeg leg;

                const std::string type_str = json_get_string(leg_json, "type");
                leg.type = (type_str == "PE" || type_str == "PUT") ? engine::OptionType::Put
                                                                     : engine::OptionType::Call;

                const std::string side_str = json_get_string(leg_json, "side");
                leg.side = (side_str == "SELL" || side_str == "sell") ? engine::Side::Sell
                                                                          : engine::Side::Buy;

                leg.strike = json_get_double(leg_json, "strike");

                leg.quantity = static_cast<int>(json_get_double(leg_json, "qty"));
                if (leg.quantity == 0) leg.quantity = static_cast<int>(json_get_double(leg_json, "quantity"));
                if (leg.quantity <= 0) leg.quantity = 1;

                leg.lot_size = static_cast<int>(json_get_double(leg_json, "lot"));
                if (leg.lot_size == 0) leg.lot_size = static_cast<int>(json_get_double(leg_json, "lot_size"));
                if (leg.lot_size <= 0) leg.lot_size = 25;

                leg.premium = json_get_double(leg_json, "premium");

                const int instrument_id = json_get_int(leg_json, "instrument_id");
                if (instrument_id > 0) leg.instrument_id = static_cast<uint32_t>(instrument_id);

                const auto sym = json_get_string(leg_json, "symbol");
                if (!sym.empty()) leg.symbol = sym;

                if (leg.strike > 0) strategy.legs.push_back(leg);
                pos = leg_end + 1;
            }
        }
    }

    return strategy;
}

static std::vector<std::string> parse_sensitivity_types_string(const std::string& body) {
    auto types = parse_string_array_after_key(body, "types");
    if (!types.empty()) return types;
    const auto single = json_get_string(body, "type");
    if (!single.empty()) return { single };
    return { "delta" };
}

} // anonymous namespace

// ============================================================================
// REST API Routes
// ============================================================================

class RestApi {
public:
    RestApi() {
        // Load config
        config::config().load();
    }
    
    void setup_routes(http::Server& server) {
        // Health check
        server.Get("/api/health", [](const http::Request&, http::Response& res) {
            JsonBuilder json;
            json.start_object()
                .key("status").value("ok")
                .key("timestamp").value(std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()))
                .key("version").value("1.0.0")
                .end_object();
            res.set_json(json.str());
        });
        
        // Config endpoint
        server.Get("/api/config", [](const http::Request&, http::Response& res) {
            auto& cfg = config::config();
            
            JsonBuilder json;
            json.start_object()
                .key("clickhouse").start_object()
                    .key("host").value(cfg.ch_host())
                    .key("port").value(cfg.ch_port())
                    .key("database").value(cfg.ch_database())
                    .key("connected").value(false)  // Would check actual connection
                .end_object()
                .key("kite").start_object()
                    .key("api_key").value(cfg.kite_api_key().empty() ? "not_set" : "***")
                    .key("authenticated").value(!cfg.kite_access_token().empty())
                .end_object()
                .key("live_mode").value(cfg.live_mode())
                .end_object();
            
            res.set_json(json.str());
        });
        
        // Calculate payoff
        server.Post("/api/payoff/calculate", [](const http::Request& req, http::Response& res) {
            // Expected: { underlying: "NIFTY", spot: 26300, legs: [...] }

            engine::Strategy strategy = parse_strategy_string(req.body);

            auto resolve_lot_size = [&](const std::string& u) -> int {
                try {
                    auto& mgr = core::get_instrument_manager();
                    if (mgr.empty()) {
                        mgr.load_with_fallback();
                    }

                    auto chain = mgr.get_option_chain(u);
                    if (!chain.empty() && chain[0]) {
                        return std::max(1, chain[0]->lot_size);
                    }

                    auto futs = mgr.get_futures(u);
                    if (!futs.empty() && futs[0]) {
                        return std::max(1, futs[0]->lot_size);
                    }
                } catch (...) {
                    // fall through
                }
                return 25;
            };

            const int default_lot_size = resolve_lot_size(strategy.underlying);
            for (auto& leg : strategy.legs) {
                if (leg.lot_size <= 0) leg.lot_size = default_lot_size;
                if (leg.quantity <= 0) leg.quantity = 1;
            }

            if (strategy.legs.empty()) {
                res.status = 400;
                res.set_json("{\"error\":\"legs required\"}");
                return;
            }

            // Optional greek assumptions
            const double iv = json_get_double(req.body, "iv");
            const int dte = json_get_int(req.body, "dte") > 0 ? json_get_int(req.body, "dte") : 30;
            const double effective_iv = iv > 0 ? iv : 0.20;
            
            // Calculate
            engine::PayoffCalculator calculator;
            auto curve = calculator.calculate_expiry_payoff(strategy);
            auto greeks = calculator.calculate_strategy_greeks(strategy, effective_iv, dte);
            
            // Build response
            JsonBuilder json;
            json.start_object()
                .key("strategy").value(strategy.name)
                .key("underlying").value(strategy.underlying)
                .key("spot").value(strategy.underlying_price)
                .key("max_profit").value(curve.max_profit)
                .key("max_loss").value(curve.max_loss)
                .key("net_premium").value(strategy.total_premium())
                .key("breakevens").start_array();
            
            for (size_t i = 0; i < curve.breakevens.size(); ++i) {
                if (i > 0) json.next();
                json.value(curve.breakevens[i]);
            }
            json.end_array();
            
            // Payoff points
            json.key("points").start_array();
            for (size_t i = 0; i < curve.points.size(); ++i) {
                if (i > 0) json.next();
                json.start_object()
                    .key("spot").value(curve.points[i].spot)
                    .key("pnl").value(curve.points[i].pnl)
                    .end_object();
            }
            json.end_array()
                .key("greeks").start_object()
                    .key("delta").value(greeks.delta)
                    .key("gamma").value(greeks.gamma)
                    .key("theta").value(greeks.theta)
                    .key("vega").value(greeks.vega)
                    .key("rho").value(greeks.rho)
                .end_object()
                .end_object();

            res.set_json(json.str());
        });
        
        // Calculate Greeks
        server.Post("/api/greeks/calculate", [](const http::Request& req, http::Response& res) {
            double spot = json_get_double(req.body, "spot");
            if (spot <= 0) spot = 26300.0;
            
            double strike = json_get_double(req.body, "strike");
            if (strike <= 0) strike = spot;
            
            double iv = json_get_double(req.body, "iv");
            if (iv <= 0) iv = 0.15;  // 15% default
            
            int dte = json_get_int(req.body, "dte");
            if (dte <= 0) dte = 26;  // Jan expiry
            
            std::string type_str = json_get_string(req.body, "type");
            engine::OptionType opt_type = (type_str == "PE") ? 
                engine::OptionType::Put : engine::OptionType::Call;
            
            // Calculate
            engine::PricingParams params;
            params.spot = spot;
            params.strike = strike;
            params.time_to_expiry = static_cast<double>(dte) / 365.0;
            params.volatility = iv;
            params.risk_free_rate = 0.07;
            
            auto greeks = engine::calculate_greeks(params, opt_type);
            double price = engine::bs_price(params, opt_type);
            
            // Response
            JsonBuilder json;
            json.start_object()
                .key("spot").value(spot)
                .key("strike").value(strike)
                .key("iv").value(iv)
                .key("dte").value(dte)
                .key("type").value(type_str.empty() ? "CE" : type_str)
                .key("price").value(price)
                .key("greeks").start_object()
                    .key("delta").value(greeks.delta)
                    .key("gamma").value(greeks.gamma)
                    .key("theta").value(greeks.theta)
                    .key("vega").value(greeks.vega)
                    .key("rho").value(greeks.rho)
                .end_object()
                .end_object();
            
            res.set_json(json.str());
        });
        
        // Option chain Greeks
        server.Get("/api/chain/greeks", [](const http::Request& req, http::Response& res) {
            double spot = req.get_param_double("spot", 26300.0);
            double iv = req.get_param_double("iv", 0.15);
            int dte = req.get_param_int("dte", 26);
            double strike_step = req.get_param_double("step", 50.0);
            int num_strikes = req.get_param_int("strikes", 21);
            
            double T = static_cast<double>(dte) / 365.0;
            
            // Generate strikes around ATM
            double start_strike = spot - (num_strikes / 2) * strike_step;
            
            JsonBuilder json;
            json.start_object()
                .key("spot").value(spot)
                .key("iv").value(iv)
                .key("dte").value(dte)
                .key("chain").start_array();
            
            for (int i = 0; i < num_strikes; ++i) {
                double K = start_strike + i * strike_step;
                
                engine::PricingParams params;
                params.spot = spot;
                params.strike = K;
                params.time_to_expiry = T;
                params.volatility = iv;
                params.risk_free_rate = 0.07;
                
                auto call_greeks = engine::calculate_greeks(params, engine::OptionType::Call);
                auto put_greeks = engine::calculate_greeks(params, engine::OptionType::Put);
                double call_price = engine::bs_call_price(params);
                double put_price = engine::bs_put_price(params);
                
                if (i > 0) json.next();
                json.start_object()
                    .key("strike").value(K)
                    .key("call").start_object()
                        .key("price").value(call_price)
                        .key("delta").value(call_greeks.delta)
                        .key("gamma").value(call_greeks.gamma)
                        .key("theta").value(call_greeks.theta)
                        .key("vega").value(call_greeks.vega)
                    .end_object()
                    .key("put").start_object()
                        .key("price").value(put_price)
                        .key("delta").value(put_greeks.delta)
                        .key("gamma").value(put_greeks.gamma)
                        .key("theta").value(put_greeks.theta)
                        .key("vega").value(put_greeks.vega)
                    .end_object()
                .end_object();
            }
            
            json.end_array().end_object();
            res.set_json(json.str());
        });
        
        // Sensitivity surface
        server.Post("/api/sensitivity", [](const http::Request& req, http::Response& res) {
            const std::string strategy_part = extract_object_after_key(req.body, "strategy");
            const std::string& effective_body = strategy_part.empty() ? req.body : strategy_part;

            engine::Strategy strategy = parse_strategy_string(effective_body);

            if (strategy.legs.empty()) {
                res.status = 400;
                res.set_json("{\"error\":\"strategy legs required\"}");
                return;
            }

            double spot_center = json_get_double(req.body, "spot");
            if (spot_center <= 0) spot_center = json_get_double(req.body, "underlying_price");
            if (spot_center <= 0 && strategy.underlying_price > 0) spot_center = strategy.underlying_price;
            if (spot_center <= 0) spot_center = 26300.0;
            strategy.underlying_price = spot_center;

            double base_iv = json_get_double(req.body, "iv");
            if (base_iv <= 0) base_iv = 0.20;

            double iv_shift_pct = json_get_double(req.body, "iv_range_pct");
            double spot_range_pct = json_get_double(req.body, "spot_range_pct");
            if (spot_range_pct == 0.0) spot_range_pct = 5.0;
            int spot_steps = json_get_int(req.body, "spot_steps");
            if (spot_steps == 0) spot_steps = 21;
            int days_range = json_get_int(req.body, "days_range");
            if (days_range == 0) days_range = 30;

            if (spot_steps < 2) spot_steps = 2;
            if (spot_steps > 60) spot_steps = 60;
            if (spot_range_pct <= 0) spot_range_pct = 5.0;
            if (days_range < 0) days_range = 0;

            const auto types = parse_sensitivity_types_string(req.body);

            const double effective_iv = base_iv * (1.0 + iv_shift_pct / 100.0);
            const double spot_range = spot_center * spot_range_pct / 100.0;
            const double min_spot = spot_center - spot_range;
            const double max_spot = spot_center + spot_range;
            const double spot_step = (spot_steps > 1)
                ? (max_spot - min_spot) / static_cast<double>(spot_steps - 1)
                : 0.0;
            const double day_step = (spot_steps > 1)
                ? static_cast<double>(days_range) / static_cast<double>(spot_steps - 1)
                : 0.0;

            std::vector<double> spots;
            spots.reserve(static_cast<size_t>(spot_steps));
            for (int i = 0; i < spot_steps; ++i) {
                spots.push_back(min_spot + spot_step * static_cast<double>(i));
            }

            std::vector<double> days_axis;
            days_axis.reserve(static_cast<size_t>(spot_steps));
            for (int i = 0; i < spot_steps; ++i) {
                days_axis.push_back(day_step * static_cast<double>(i));
            }

            engine::PayoffCalculator calculator;
            JsonBuilder out;
            out.start_array();
            bool any_surface = false;

            for (const auto& type : types) {
                if (type != "delta" && type != "gamma" && type != "theta" &&
                    type != "vega" && type != "pnl") {
                    continue;
                }

                if (any_surface) out.next();
                any_surface = true;

                out.start_object()
                    .key("type").value(type)
                    .key("spot_center").value(spot_center)
                    .key("strike").value(0.0)
                    .key("iv").value(effective_iv)
                    .key("surface").start_array();

                for (size_t si = 0; si < spots.size(); ++si) {
                    if (si > 0) out.next();
                    const double spot = spots[si];

                    out.start_object()
                        .key("spot").value(spot)
                        .key("values").start_array();

                    for (size_t di = 0; di < days_axis.size(); ++di) {
                        if (di > 0) out.next();
                        const double day_value = days_axis[di];
                        const double tte_days = std::max(0.0, static_cast<double>(days_range) - day_value);
                        const double tte_years = std::max(tte_days / 365.0, 1e-6);

                        double value = 0.0;
                        if (type == "pnl") {
                            for (const auto& leg : strategy.legs) {
                                value += calculator.calculate_leg_pnl_with_greeks(
                                    leg, spot, effective_iv, tte_years);
                            }
                        } else {
                            engine::Greeks greeks;
                            for (const auto& leg : strategy.legs) {
                                greeks += calculator.calculate_leg_greeks(
                                    leg, spot, effective_iv, tte_years);
                            }

                            if (type == "delta") value = greeks.delta;
                            else if (type == "gamma") value = greeks.gamma;
                            else if (type == "theta") value = greeks.theta;
                            else if (type == "vega") value = greeks.vega;
                        }

                        out.start_object()
                            .key("days").value(day_value)
                            .key("value").value(value)
                            .end_object();
                    }

                    out.end_array().end_object();
                }

                out.end_array().end_object();
            }

            out.end_array();

            if (!any_surface) {
                res.status = 400;
                res.set_json("{\"error\":\"no valid sensitivity types\"}");
                return;
            }

            res.set_json(out.str());
        });
        
        // IV calculation
        server.Post("/api/iv/calculate", [](const http::Request& req, http::Response& res) {
            double spot = json_get_double(req.body, "spot");
            double strike = json_get_double(req.body, "strike");
            double price = json_get_double(req.body, "price");
            int dte = json_get_int(req.body, "dte");
            std::string type_str = json_get_string(req.body, "type");
            
            if (spot <= 0 || strike <= 0 || price <= 0 || dte <= 0) {
                res.status = 400;
                res.set_json("{\"error\": \"Missing required params: spot, strike, price, dte\"}");
                return;
            }
            
            engine::PricingParams params;
            params.spot = spot;
            params.strike = strike;
            params.time_to_expiry = static_cast<double>(dte) / 365.0;
            params.risk_free_rate = 0.07;
            
            engine::OptionType opt_type = (type_str == "PE") ? 
                engine::OptionType::Put : engine::OptionType::Call;
            
            auto iv = engine::calculate_iv(price, params, opt_type);
            
            JsonBuilder json;
            json.start_object()
                .key("spot").value(spot)
                .key("strike").value(strike)
                .key("price").value(price)
                .key("dte").value(dte)
                .key("type").value(type_str.empty() ? "CE" : type_str);
            
            if (iv) {
                json.key("iv").value(*iv)
                    .key("iv_pct").value(*iv * 100);
            } else {
                json.key("iv").raw("null")
                    .key("error").value("IV calculation failed to converge");
            }
            
            json.end_object();
            res.set_json(json.str());
        });
        
        // Market data (placeholder)
        server.Get("/api/quote", [](const http::Request& req, http::Response& res) {
            std::string symbol = req.get_param("symbol", "NIFTY");
            
            // In production, would fetch from Kite or ClickHouse
            JsonBuilder json;
            json.start_object()
                .key("symbol").value(symbol)
                .key("ltp").value(26300.0)
                .key("change").value(125.50)
                .key("change_pct").value(0.48)
                .key("volume").value(static_cast<int64_t>(15000000))
                .key("oi").value(static_cast<int64_t>(12500000))
                .key("bid").value(26299.50)
                .key("ask").value(26300.50)
                .key("timestamp").value(std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()))
                .end_object();
            
            res.set_json(json.str());
        });
    }
};

// ============================================================================
// Server Startup
// ============================================================================

// Forward declaration from screener_routes.cpp
void setup_screener_routes(http::Server& server);

// Forward declaration from replay_routes.cpp
void setup_replay_routes(http::Server& server);

// Forward declarations from live_routes.cpp
std::string handle_live_subscribe(const std::string& body);
std::string handle_live_unsubscribe(const std::string& body);
std::string handle_live_subscribe_option_chain(const std::string& body);
std::string handle_live_unsubscribe_option_chain(const std::string& body);
std::string handle_live_get_subscriptions();
std::string handle_live_get_stats();
std::string handle_live_add_credentials(const std::string& body);
std::string handle_live_remove_credentials(size_t index);
std::string handle_live_start();
std::string handle_live_stop();
std::string handle_live_replace_subscriptions(const std::string& body);
void init_live_data_service(
    std::shared_ptr<core::InstrumentManager> instrument_manager,
    std::shared_ptr<cache::MarketCache> market_cache);

void setup_live_routes(http::Server& server) {
    // Subscribe to symbols
    server.Post("/api/live/subscribe", [](const http::Request& req, http::Response& res) {
        res.set_json(handle_live_subscribe(req.body));
    });
    
    // Unsubscribe from symbols
    server.Post("/api/live/unsubscribe", [](const http::Request& req, http::Response& res) {
        res.set_json(handle_live_unsubscribe(req.body));
    });
    
    // Subscribe to option chain
    server.Post("/api/live/subscribe/option-chain", [](const http::Request& req, http::Response& res) {
        res.set_json(handle_live_subscribe_option_chain(req.body));
    });
    
    // Unsubscribe from option chain
    server.Post("/api/live/unsubscribe/option-chain", [](const http::Request& req, http::Response& res) {
        res.set_json(handle_live_unsubscribe_option_chain(req.body));
    });
    
    // Get current subscriptions
    server.Get("/api/live/subscriptions", [](const http::Request&, http::Response& res) {
        res.set_json(handle_live_get_subscriptions());
    });
    
    // Get stats
    server.Get("/api/live/stats", [](const http::Request&, http::Response& res) {
        res.set_json(handle_live_get_stats());
    });
    
    // Add credentials
    server.Post("/api/live/credentials", [](const http::Request& req, http::Response& res) {
        res.set_json(handle_live_add_credentials(req.body));
    });
    
    // Start live streaming
    server.Post("/api/live/start", [](const http::Request&, http::Response& res) {
        res.set_json(handle_live_start());
    });
    
    // Stop live streaming
    server.Post("/api/live/stop", [](const http::Request&, http::Response& res) {
        res.set_json(handle_live_stop());
    });
    
    // Replace subscriptions (efficient switch between chains)
    server.Post("/api/live/replace", [](const http::Request& req, http::Response& res) {
        res.set_json(handle_live_replace_subscriptions(req.body));
    });
}

void start_rest_server(int port) {
    http::Server server;
    server.enable_cors();
    
    // ========================================================================
    // Configuration Loading
    // ========================================================================
    auto& cfg = config::config();
    if (!cfg.is_loaded()) {
        cfg.load();
    }
    
    if (cfg.is_loaded()) {
        std::cout << "\n[Config] ✓ Loaded from: " << cfg.loaded_path() << "\n";
    } else {
        std::cout << "\n[Config] ⚠ No .env file found, using defaults/environment\n";
    }
    
    // ========================================================================
    // Datasource Initialization
    // ========================================================================
    std::cout << "\n[Datasource] Initializing datasources...\n";
    
    // 1. Test ClickHouse connectivity
    {
        auto ch_config = core::ClickHouseHttpConfig::from_config();
        std::cout << "[Datasource] ClickHouse: " << ch_config.host << ":" << ch_config.port 
                  << "/" << ch_config.database << "\n";
        
        if (core::clickhouse_ping(ch_config)) {
            std::cout << "[Datasource] ✓ ClickHouse connection OK\n";
            
            // Test query to verify tick data access
            try {
                auto result = core::clickhouse_execute_query(ch_config, 
                    "SELECT count() FROM tick_data LIMIT 1");
                std::cout << "[Datasource] ✓ ClickHouse tick_data accessible\n";
            } catch (const std::exception& e) {
                std::cout << "[Datasource] ⚠ ClickHouse tick_data: " << e.what() << "\n";
            }
        } else {
            std::cout << "[Datasource] ✗ ClickHouse connection FAILED\n";
        }
    }
    
    // 2. Load instruments (from Kite API with ClickHouse ID mapping)
    auto& mgr = core::get_instrument_manager();
    if (mgr.empty()) {
        std::cout << "\n[Datasource] Loading instruments...\n";
        mgr.load_with_fallback();
    }
    std::cout << "[Datasource] ✓ InstrumentManager: " << mgr.size() << " instruments loaded\n";
    
    // ========================================================================
    // REST API Setup
    // ========================================================================
    RestApi api;
    api.setup_routes(server);
    
    // Add screener routes
    setup_screener_routes(server);
    
    // Add replay routes
    setup_replay_routes(server);
    
    // Initialize and add live routes (includes Kite WS setup)
    {
        // Create shared_ptr that points to the global instance (don't own it)
        auto* mgr_ptr = &mgr;
        auto instrument_manager = std::shared_ptr<core::InstrumentManager>(
            mgr_ptr, [](core::InstrumentManager*) { /* no-op deleter */ });
        
        auto market_cache = std::make_shared<cache::MarketCache>();
        
        std::cout << "\n[Datasource] Initializing Kite WebSocket...\n";
        init_live_data_service(instrument_manager, market_cache);
        setup_live_routes(server);
    }
    
    // ========================================================================
    // WebSocket Server for Frontend
    // ========================================================================
    int ws_port = cfg.get_int("WS_PORT", 8081);
    std::cout << "\n[WebSocket] Starting WebSocket server on port " << ws_port << "...\n";
    start_websocket_server(ws_port);
    std::cout << "[WebSocket] ✓ Frontend can connect to ws://localhost:" << ws_port << "\n";
    
    std::cout << "\nStarting REST API server on port " << port << std::endl;
    std::cout << "Screener endpoints available at /api/screener/*" << std::endl;
    std::cout << "Replay endpoints available at /api/replay/*" << std::endl;
    std::cout << "Live endpoints available at /api/live/*" << std::endl;
    server.listen("0.0.0.0", port);
}

} // namespace payoff::api
