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
#include "core/config.hpp"
#include "core/models.hpp"
#include "core/instrument_manager.hpp"
#include "payoff/calculator.hpp"
#include "payoff/pricing.hpp"
#include "payoff/models.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>

namespace payoff::api {

// ============================================================================
// JSON Helpers
// ============================================================================

namespace {

// Simple JSON builder (for demo - use nlohmann::json in production)
class JsonBuilder {
public:
    JsonBuilder& start_object() {
        ss_ << "{";
        first_ = true;
        return *this;
    }
    
    JsonBuilder& end_object() {
        ss_ << "}";
        return *this;
    }
    
    JsonBuilder& start_array() {
        ss_ << "[";
        first_ = true;
        return *this;
    }
    
    JsonBuilder& end_array() {
        ss_ << "]";
        return *this;
    }
    
    JsonBuilder& key(const std::string& k) {
        if (!first_) ss_ << ",";
        ss_ << "\"" << k << "\":";
        first_ = false;
        return *this;
    }
    
    JsonBuilder& value(const std::string& v) {
        ss_ << "\"" << escape(v) << "\"";
        return *this;
    }

    JsonBuilder& value(const char* v) {
        ss_ << "\"" << escape(v ? std::string(v) : std::string()) << "\"";
        return *this;
    }
    
    JsonBuilder& value(double v) {
        ss_ << std::fixed << std::setprecision(4) << v;
        return *this;
    }
    
    JsonBuilder& value(int v) {
        ss_ << v;
        return *this;
    }
    
    JsonBuilder& value(int64_t v) {
        ss_ << v;
        return *this;
    }
    
    JsonBuilder& value(bool v) {
        ss_ << (v ? "true" : "false");
        return *this;
    }
    
    JsonBuilder& raw(const std::string& r) {
        ss_ << r;
        return *this;
    }
    
    JsonBuilder& next() {
        first_ = false;
        return *this;
    }
    
    std::string str() const { return ss_.str(); }
    
private:
    std::ostringstream ss_;
    bool first_ = true;
    
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
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
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
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
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
            // Parse strategy from body
            // Expected: {underlying: "NIFTY", spot: 26300, legs: [...]}
            
            double spot = json_get_double(req.body, "spot");
            if (spot <= 0) spot = 26300.0;  // Default NIFTY
            
            std::string underlying = json_get_string(req.body, "underlying");
            if (underlying.empty()) underlying = "NIFTY";
            
            // Create demo strategy (Bull Call Spread)
            engine::Strategy strategy;
            strategy.name = "Bull Call Spread";
            strategy.underlying = underlying;
            strategy.underlying_price = spot;
            
            // Buy ATM Call
            engine::OptionLeg long_call;
            long_call.type = engine::OptionType::Call;
            long_call.side = engine::Side::Buy;
            long_call.strike = spot;
            long_call.quantity = 1;
            long_call.lot_size = 25;
            long_call.premium = 250.0;
            strategy.legs.push_back(long_call);
            
            // Sell OTM Call
            engine::OptionLeg short_call;
            short_call.type = engine::OptionType::Call;
            short_call.side = engine::Side::Sell;
            short_call.strike = spot + 200;
            short_call.quantity = 1;
            short_call.lot_size = 25;
            short_call.premium = 150.0;
            strategy.legs.push_back(short_call);
            
            // Calculate
            engine::PayoffCalculator calculator;
            auto curve = calculator.calculate_expiry_payoff(strategy);
            
            // Build response
            JsonBuilder json;
            json.start_object()
                .key("strategy").value(strategy.name)
                .key("underlying").value(underlying)
                .key("spot").value(spot)
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
            json.end_array().end_object();
            
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
            double spot = json_get_double(req.body, "spot");
            if (spot <= 0) spot = 26300.0;
            
            double strike = json_get_double(req.body, "strike");
            if (strike <= 0) strike = spot;
            
            double iv = json_get_double(req.body, "iv");
            if (iv <= 0) iv = 0.15;
            
            std::string surface_type = json_get_string(req.body, "type");
            if (surface_type.empty()) surface_type = "delta";
            
            // Generate surface (spot x time)
            int spot_steps = 11;
            int time_steps = 6;
            double spot_range = 0.10;  // ±10%
            
            JsonBuilder json;
            json.start_object()
                .key("type").value(surface_type)
                .key("spot_center").value(spot)
                .key("strike").value(strike)
                .key("iv").value(iv)
                .key("surface").start_array();
            
            for (int i = 0; i < spot_steps; ++i) {
                double S = spot * (1.0 - spot_range + 2.0 * spot_range * i / (spot_steps - 1));
                
                if (i > 0) json.next();
                json.start_object()
                    .key("spot").value(S)
                    .key("values").start_array();
                
                for (int j = 0; j < time_steps; ++j) {
                    double T = (5.0 + j * 10.0) / 365.0;  // 5, 15, 25, 35, 45, 55 days
                    
                    engine::PricingParams params;
                    params.spot = S;
                    params.strike = strike;
                    params.time_to_expiry = T;
                    params.volatility = iv;
                    params.risk_free_rate = 0.07;
                    
                    double value = 0.0;
                    if (surface_type == "delta") {
                        value = engine::calculate_delta(params, engine::OptionType::Call);
                    } else if (surface_type == "gamma") {
                        value = engine::calculate_gamma(params);
                    } else if (surface_type == "theta") {
                        value = engine::calculate_theta(params, engine::OptionType::Call);
                    } else if (surface_type == "vega") {
                        value = engine::calculate_vega(params);
                    }
                    
                    if (j > 0) json.next();
                    json.start_object()
                        .key("days").value(static_cast<int>(T * 365))
                        .key("value").value(value)
                    .end_object();
                }
                
                json.end_array().end_object();
            }
            
            json.end_array().end_object();
            res.set_json(json.str());
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

void start_rest_server(int port) {
    http::Server server;
    server.enable_cors();
    
    RestApi api;
    api.setup_routes(server);
    
    // Add screener routes
    setup_screener_routes(server);
    
    // Add replay routes
    setup_replay_routes(server);
    
    std::cout << "Starting REST API server on port " << port << std::endl;
    std::cout << "Screener endpoints available at /api/screener/*" << std::endl;
    std::cout << "Replay endpoints available at /api/replay/*" << std::endl;
    server.listen("0.0.0.0", port);
}

} // namespace payoff::api
