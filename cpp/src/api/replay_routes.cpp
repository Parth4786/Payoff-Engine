/**
 * @file replay_routes.cpp
 * @brief REST API routes for strategy replay
 * 
 * Endpoints:
 * - POST /api/replay/strategy     - Run strategy through historical data
 * - POST /api/replay/prediction   - Get predicted payoff at historical time T
 * - POST /api/replay/compare      - Compare prediction vs reality
 * - POST /api/replay/session/create   - Create replay session
 * - GET  /api/replay/session/:id/state - Get session state
 * - POST /api/replay/session/:id/step  - Step session forward
 * - POST /api/replay/session/:id/seek  - Seek to timestamp
 * - DELETE /api/replay/session/:id     - Delete session
 * - GET  /api/replay/events       - Get market events
 * - POST /api/payoff/historical-batch - Batch historical payoff
 */

#include "api/http_server.hpp"
#include "api/replay_service.hpp"

#include <iomanip>
#include <memory>
#include <sstream>

namespace payoff::api {

// ============================================================================
// JSON Builder (shared with other routes)
// ============================================================================

namespace {

class ReplayJsonBuilder {
public:
    ReplayJsonBuilder& start_object() { ss_ << "{"; first_ = true; return *this; }
    ReplayJsonBuilder& end_object() { ss_ << "}"; return *this; }
    ReplayJsonBuilder& start_array() { ss_ << "["; first_ = true; return *this; }
    ReplayJsonBuilder& end_array() { ss_ << "]"; return *this; }
    
    ReplayJsonBuilder& key(const std::string& k) {
        if (!first_) ss_ << ",";
        ss_ << "\"" << k << "\":";
        first_ = false;
        return *this;
    }
    
    ReplayJsonBuilder& value(const std::string& v) { 
        ss_ << "\"" << escape(v) << "\""; 
        return *this; 
    }
    ReplayJsonBuilder& value(double v) { 
        ss_ << std::fixed << std::setprecision(4) << v; 
        return *this; 
    }
    ReplayJsonBuilder& value(int v) { ss_ << v; return *this; }
    ReplayJsonBuilder& value(int64_t v) { ss_ << v; return *this; }
    ReplayJsonBuilder& value(uint32_t v) { ss_ << v; return *this; }
    ReplayJsonBuilder& value(size_t v) { ss_ << v; return *this; }
    ReplayJsonBuilder& value(bool v) { ss_ << (v ? "true" : "false"); return *this; }
    ReplayJsonBuilder& null_value() { ss_ << "null"; return *this; }
    ReplayJsonBuilder& next() { ss_ << ","; first_ = false; return *this; }
    
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

// Parse JSON helpers
int64_t parse_timestamp(const std::string& s) {
    try { return std::stoll(s); } catch (...) { return 0; }
}

double parse_double(const std::string& s) {
    try { return std::stod(s); } catch (...) { return 0.0; }
}

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
    
    return parse_double(json.substr(pos, end - pos));
}

int64_t json_get_int64(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    
    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
    auto end = json.find_first_of(",}]", pos);
    if (end == std::string::npos) return 0;
    
    return parse_timestamp(json.substr(pos, end - pos));
}

bool json_get_bool(const std::string& json, const std::string& key) {
    return json.find("\"" + key + "\":true") != std::string::npos;
}

// Parse strategy from JSON
engine::Strategy parse_strategy(const std::string& json) {
    engine::Strategy strategy;
    
    strategy.underlying = json_get_string(json, "underlying");
    if (strategy.underlying.empty()) strategy.underlying = "NIFTY";
    
    strategy.underlying_price = json_get_double(json, "spot");
    if (strategy.underlying_price <= 0) strategy.underlying_price = json_get_double(json, "underlying_price");
    if (strategy.underlying_price <= 0) strategy.underlying_price = 26300.0;
    
    // Parse legs array
    auto legs_pos = json.find("\"legs\"");
    if (legs_pos != std::string::npos) {
        auto arr_start = json.find('[', legs_pos);
        auto arr_end = json.find(']', arr_start);
        if (arr_start != std::string::npos && arr_end != std::string::npos) {
            std::string legs_str = json.substr(arr_start, arr_end - arr_start + 1);
            
            // Parse each leg
            size_t pos = 0;
            while ((pos = legs_str.find('{', pos)) != std::string::npos) {
                auto leg_end = legs_str.find('}', pos);
                if (leg_end == std::string::npos) break;
                
                std::string leg_json = legs_str.substr(pos, leg_end - pos + 1);
                
                engine::OptionLeg leg;
                
                // Type
                std::string type_str = json_get_string(leg_json, "type");
                if (type_str == "PE" || type_str == "PUT") {
                    leg.type = engine::OptionType::Put;
                } else {
                    leg.type = engine::OptionType::Call;
                }
                
                // Side
                std::string side_str = json_get_string(leg_json, "side");
                if (side_str == "SELL" || side_str == "sell") {
                    leg.side = engine::Side::Sell;
                } else {
                    leg.side = engine::Side::Buy;
                }
                
                leg.strike = json_get_double(leg_json, "strike");
                leg.quantity = static_cast<int>(json_get_double(leg_json, "qty"));
                if (leg.quantity == 0) leg.quantity = static_cast<int>(json_get_double(leg_json, "quantity"));
                if (leg.quantity == 0) leg.quantity = 1;
                
                leg.lot_size = static_cast<int>(json_get_double(leg_json, "lot"));
                if (leg.lot_size == 0) leg.lot_size = static_cast<int>(json_get_double(leg_json, "lot_size"));
                if (leg.lot_size == 0) leg.lot_size = 25;
                
                leg.premium = json_get_double(leg_json, "premium");
                
                if (leg.strike > 0) {
                    strategy.legs.push_back(leg);
                }
                
                pos = leg_end + 1;
            }
        }
    }
    
    return strategy;
}

// Global replay service instance
std::unique_ptr<replay::ReplayService> g_replay_service;

replay::ReplayService& get_replay_service() {
    if (!g_replay_service) {
        g_replay_service = replay::create_replay_service();
    }
    return *g_replay_service;
}

} // anonymous namespace

// ============================================================================
// Replay API Routes
// ============================================================================

class ReplayApi {
public:
    void setup_routes(http::Server& server) {
        
        // ====================================================================
        // POST /api/replay/strategy - Run strategy through historical data
        // ====================================================================
        server.Post("/api/replay/strategy", [](const http::Request& req, http::Response& res) {
            replay::StrategyReplayRequest request;
            request.strategy = parse_strategy(req.body);
            request.start = core::Timestamp(json_get_int64(req.body, "start_timestamp"));
            request.end = core::Timestamp(json_get_int64(req.body, "end_timestamp"));
            request.interval_ms = json_get_int64(req.body, "interval_ms");
            if (request.interval_ms <= 0) request.interval_ms = 60000;
            request.include_greeks = json_get_bool(req.body, "include_greeks");
            
            if (request.start.count() == 0 || request.end.count() == 0) {
                res.status = 400;
                res.set_json("{\"error\": \"start_timestamp and end_timestamp required\"}");
                return;
            }
            
            auto result = get_replay_service().replay_strategy(request);
            
            ReplayJsonBuilder json;
            json.start_object()
                .key("snapshots").start_array();
            
            for (size_t i = 0; i < result.snapshots.size(); ++i) {
                if (i > 0) json.next();
                const auto& snap = result.snapshots[i];
                json.start_object()
                    .key("timestamp").value(snap.timestamp.count())
                    .key("underlying_price").value(snap.underlying_price)
                    .key("total_pnl").value(snap.total_pnl)
                    .key("greeks").start_object()
                        .key("delta").value(snap.delta)
                        .key("gamma").value(snap.gamma)
                        .key("theta").value(snap.theta)
                        .key("vega").value(snap.vega)
                    .end_object();
                
                json.key("leg_prices").start_array();
                for (size_t j = 0; j < snap.leg_prices.size(); ++j) {
                    if (j > 0) json.next();
                    json.start_object()
                        .key("strike").value(snap.leg_prices[j].strike)
                        .key("price").value(snap.leg_prices[j].price)
                        .key("iv").value(snap.leg_prices[j].iv)
                    .end_object();
                }
                json.end_array().end_object();
            }
            
            json.end_array()
                .key("summary").start_object()
                    .key("initial_pnl").value(result.initial_pnl)
                    .key("final_pnl").value(result.final_pnl)
                    .key("max_pnl").value(result.max_pnl)
                    .key("min_pnl").value(result.min_pnl)
                    .key("pnl_std_dev").value(result.pnl_std_dev)
                .end_object()
            .end_object();
            
            res.set_json(json.str());
        });
        
        // ====================================================================
        // POST /api/replay/prediction - Get predicted payoff at historical time T
        // ====================================================================
        server.Post("/api/replay/prediction", [](const http::Request& req, http::Response& res) {
            replay::PredictionRequest request;
            request.strategy = parse_strategy(req.body);
            request.as_of_timestamp = core::Timestamp(json_get_int64(req.body, "as_of_timestamp"));
            
            // Parse prediction horizons
            auto horizons_pos = req.body.find("\"prediction_horizons\"");
            if (horizons_pos != std::string::npos) {
                auto arr_start = req.body.find('[', horizons_pos);
                auto arr_end = req.body.find(']', arr_start);
                if (arr_start != std::string::npos && arr_end != std::string::npos) {
                    std::string horizons_str = req.body.substr(arr_start, arr_end - arr_start + 1);
                    
                    size_t pos = 0;
                    while ((pos = horizons_str.find('{', pos)) != std::string::npos) {
                        auto h_end = horizons_str.find('}', pos);
                        if (h_end == std::string::npos) break;
                        
                        std::string h_json = horizons_str.substr(pos, h_end - pos + 1);
                        
                        replay::PredictionHorizon horizon;
                        horizon.days_forward = static_cast<int>(json_get_double(h_json, "days_forward"));
                        if (horizon.days_forward <= 0) horizon.days_forward = 1;
                        
                        double iv_shift = json_get_double(h_json, "iv_shift_pct");
                        if (iv_shift != 0) horizon.iv_shift_pct = iv_shift;
                        
                        request.horizons.push_back(horizon);
                        pos = h_end + 1;
                    }
                }
            }
            
            if (request.horizons.empty()) {
                request.horizons.push_back({1, std::nullopt, std::nullopt});
            }
            
            auto result = get_replay_service().get_prediction(request);
            
            ReplayJsonBuilder json;
            json.start_object()
                .key("as_of_timestamp").value(result.as_of_timestamp.count())
                .key("market_state_at_time").start_object()
                    .key("underlying_price").value(result.market_state.underlying_price)
                    .key("atm_iv").value(result.market_state.atm_iv)
                    .key("days_to_expiry").value(result.market_state.days_to_expiry)
                .end_object()
                .key("predictions").start_array();
            
            for (size_t i = 0; i < result.predictions.size(); ++i) {
                if (i > 0) json.next();
                const auto& pred = result.predictions[i];
                json.start_object()
                    .key("horizon").start_object()
                        .key("days_forward").value(pred.horizon.days_forward);
                if (pred.horizon.iv_shift_pct) {
                    json.key("iv_shift_pct").value(*pred.horizon.iv_shift_pct);
                }
                json.end_object()
                    .key("predicted_payoff").start_array();
                
                for (size_t j = 0; j < std::min(pred.payoff_curve.size(), size_t(50)); ++j) {
                    if (j > 0) json.next();
                    json.start_object()
                        .key("spot").value(pred.payoff_curve[j].spot)
                        .key("pnl").value(pred.payoff_curve[j].pnl)
                    .end_object();
                }
                json.end_array()
                    .key("predicted_greeks").start_object()
                        .key("delta").value(pred.predicted_greeks.delta)
                        .key("gamma").value(pred.predicted_greeks.gamma)
                        .key("theta").value(pred.predicted_greeks.theta)
                        .key("vega").value(pred.predicted_greeks.vega)
                    .end_object()
                .end_object();
            }
            
            json.end_array().end_object();
            res.set_json(json.str());
        });
        
        // ====================================================================
        // POST /api/replay/compare - Compare prediction vs reality
        // ====================================================================
        server.Post("/api/replay/compare", [](const http::Request& req, http::Response& res) {
            replay::CompareRequest request;
            request.strategy = parse_strategy(req.body);
            request.prediction_timestamp = core::Timestamp(json_get_int64(req.body, "prediction_timestamp"));
            request.actual_timestamp = core::Timestamp(json_get_int64(req.body, "actual_timestamp"));
            
            if (request.prediction_timestamp.count() == 0 || request.actual_timestamp.count() == 0) {
                res.status = 400;
                res.set_json("{\"error\": \"prediction_timestamp and actual_timestamp required\"}");
                return;
            }
            
            auto result = get_replay_service().compare_prediction_vs_actual(request);
            
            ReplayJsonBuilder json;
            json.start_object()
                .key("prediction_timestamp").value(result.prediction_timestamp.count())
                .key("actual_timestamp").value(result.actual_timestamp.count())
                .key("time_elapsed_hours").value(result.time_elapsed_hours)
                .key("at_prediction_time").start_object()
                    .key("underlying_price").value(result.at_prediction.underlying_price)
                    .key("predicted_pnl_at_current_spot").value(result.at_prediction.predicted_pnl)
                    .key("predicted_breakeven").value(result.at_prediction.predicted_breakeven)
                .end_object()
                .key("at_actual_time").start_object()
                    .key("underlying_price").value(result.at_actual.underlying_price)
                    .key("actual_pnl").value(result.at_actual.actual_pnl)
                    .key("actual_breakeven").value(result.at_actual.actual_breakeven)
                .end_object()
                .key("deviation").start_object()
                    .key("pnl_deviation").value(result.deviation.pnl_deviation)
                    .key("pnl_deviation_pct").value(result.deviation.pnl_deviation_pct)
                    .key("breakeven_shift").value(result.deviation.breakeven_shift)
                    .key("iv_change").value(result.deviation.iv_change)
                    .key("delta_drift").value(result.deviation.delta_drift)
                    .key("prediction_accuracy_score").value(result.deviation.prediction_accuracy_score)
                .end_object()
                .key("insights").start_array();
            
            for (size_t i = 0; i < result.insights.size(); ++i) {
                if (i > 0) json.next();
                json.value(result.insights[i]);
            }
            
            json.end_array().end_object();
            res.set_json(json.str());
        });
        
        // ====================================================================
        // POST /api/replay/session/create - Create replay session
        // ====================================================================
        server.Post("/api/replay/session/create", [](const http::Request& req, http::Response& res) {
            auto strategy = parse_strategy(req.body);
            auto start = core::Timestamp(json_get_int64(req.body, "start_timestamp"));
            auto end = core::Timestamp(json_get_int64(req.body, "end_timestamp"));
            double speed = json_get_double(req.body, "speed");
            if (speed <= 0) speed = 1.0;
            int64_t interval = json_get_int64(req.body, "payoff_interval_ms");
            if (interval <= 0) interval = 60000;
            
            if (start.count() == 0 || end.count() == 0) {
                res.status = 400;
                res.set_json("{\"error\": \"start_timestamp and end_timestamp required\"}");
                return;
            }
            
            std::string session_id = get_replay_service().create_session(
                strategy, start, end, speed, interval);
            
            ReplayJsonBuilder json;
            json.start_object()
                .key("session_id").value(session_id)
                .key("status").value("created")
            .end_object();
            
            res.set_json(json.str());
        });
        
        // ====================================================================
        // GET /api/replay/session/:id/state - Get session state
        // ====================================================================
        server.Get("/api/replay/session/state", [](const http::Request& req, http::Response& res) {
            std::string session_id = req.get_param("id", "");
            if (session_id.empty()) {
                res.status = 400;
                res.set_json("{\"error\": \"session id required\"}");
                return;
            }
            
            auto session_opt = get_replay_service().get_session(session_id);
            if (!session_opt) {
                res.status = 404;
                res.set_json("{\"error\": \"session not found\"}");
                return;
            }
            
            const auto& session = *session_opt;
            
            ReplayJsonBuilder json;
            json.start_object()
                .key("session_id").value(session.session_id)
                .key("current_timestamp").value(session.current.count())
                .key("progress_pct").value(session.progress_pct)
                .key("has_more").value(session.has_more)
                .key("current_snapshot").start_object()
                    .key("timestamp").value(session.current_snapshot.timestamp.count())
                    .key("underlying_price").value(session.current_snapshot.underlying_price)
                    .key("total_pnl").value(session.current_snapshot.total_pnl)
                    .key("delta").value(session.current_snapshot.delta)
                    .key("gamma").value(session.current_snapshot.gamma)
                    .key("theta").value(session.current_snapshot.theta)
                    .key("vega").value(session.current_snapshot.vega)
                .end_object()
            .end_object();
            
            res.set_json(json.str());
        });
        
        // ====================================================================
        // POST /api/replay/session/step - Step session forward
        // ====================================================================
        server.Post("/api/replay/session/step", [](const http::Request& req, http::Response& res) {
            std::string session_id = json_get_string(req.body, "session_id");
            if (session_id.empty()) session_id = req.get_param("id", "");
            
            if (session_id.empty()) {
                res.status = 400;
                res.set_json("{\"error\": \"session_id required\"}");
                return;
            }
            
            auto snap_opt = get_replay_service().step_session(session_id);
            if (!snap_opt) {
                res.status = 404;
                res.set_json("{\"error\": \"session not found or ended\"}");
                return;
            }
            
            const auto& snap = *snap_opt;
            auto session_opt = get_replay_service().get_session(session_id);
            
            ReplayJsonBuilder json;
            json.start_object()
                .key("session_id").value(session_id)
                .key("current_timestamp").value(snap.timestamp.count())
                .key("progress_pct").value(session_opt ? session_opt->progress_pct : 100.0)
                .key("market_state").start_object()
                    .key("underlying_price").value(snap.underlying_price)
                .end_object()
                .key("payoff_snapshot").start_object()
                    .key("total_pnl").value(snap.total_pnl)
                    .key("greeks").start_object()
                        .key("delta").value(snap.delta)
                        .key("gamma").value(snap.gamma)
                        .key("theta").value(snap.theta)
                        .key("vega").value(snap.vega)
                    .end_object()
                .end_object()
                .key("has_more").value(session_opt ? session_opt->has_more : false)
            .end_object();
            
            res.set_json(json.str());
        });
        
        // ====================================================================
        // POST /api/replay/session/seek - Seek to timestamp
        // ====================================================================
        server.Post("/api/replay/session/seek", [](const http::Request& req, http::Response& res) {
            std::string session_id = json_get_string(req.body, "session_id");
            int64_t target_ts = json_get_int64(req.body, "target_timestamp");
            
            if (session_id.empty() || target_ts == 0) {
                res.status = 400;
                res.set_json("{\"error\": \"session_id and target_timestamp required\"}");
                return;
            }
            
            bool success = get_replay_service().seek_session(
                session_id, core::Timestamp(target_ts));
            
            if (!success) {
                res.status = 404;
                res.set_json("{\"error\": \"session not found\"}");
                return;
            }
            
            auto session_opt = get_replay_service().get_session(session_id);
            
            ReplayJsonBuilder json;
            json.start_object()
                .key("session_id").value(session_id)
                .key("status").value("seeked")
                .key("current_timestamp").value(session_opt ? session_opt->current.count() : target_ts)
                .key("progress_pct").value(session_opt ? session_opt->progress_pct : 0.0)
            .end_object();
            
            res.set_json(json.str());
        });
        
        // ====================================================================
        // DELETE /api/replay/session/:id - Delete session
        // ====================================================================
        server.Delete("/api/replay/session", [](const http::Request& req, http::Response& res) {
            std::string session_id = req.get_param("id", "");
            
            if (session_id.empty()) {
                res.status = 400;
                res.set_json("{\"error\": \"session id required\"}");
                return;
            }
            
            bool success = get_replay_service().delete_session(session_id);
            
            ReplayJsonBuilder json;
            json.start_object()
                .key("session_id").value(session_id)
                .key("deleted").value(success)
            .end_object();
            
            res.set_json(json.str());
        });
        
        // ====================================================================
        // GET /api/replay/events - Get market events
        // ====================================================================
        server.Get("/api/replay/events", [](const http::Request& req, http::Response& res) {
            std::string underlying = req.get_param("underlying", "NIFTY");
            int64_t start = parse_timestamp(req.get_param("start", "0"));
            int64_t end = parse_timestamp(req.get_param("end", "0"));
            
            if (start == 0 || end == 0) {
                res.status = 400;
                res.set_json("{\"error\": \"start and end timestamps required\"}");
                return;
            }
            
            auto events = get_replay_service().get_events(
                underlying, core::Timestamp(start), core::Timestamp(end));
            
            ReplayJsonBuilder json;
            json.start_object()
                .key("events").start_array();
            
            for (size_t i = 0; i < events.size(); ++i) {
                if (i > 0) json.next();
                const auto& evt = events[i];
                json.start_object()
                    .key("timestamp").value(evt.timestamp.count())
                    .key("type").value(evt.type)
                    .key("description").value(evt.description)
                    .key("magnitude").value(evt.magnitude);
                
                if (evt.strike) {
                    json.key("strike").value(*evt.strike);
                }
                if (evt.option_type) {
                    json.key("option_type").value(*evt.option_type);
                }
                
                json.end_object();
            }
            
            json.end_array().end_object();
            res.set_json(json.str());
        });
        
        // ====================================================================
        // POST /api/payoff/historical-batch - Batch historical payoff
        // ====================================================================
        server.Post("/api/payoff/historical-batch", [](const http::Request& req, http::Response& res) {
            replay::BatchPayoffRequest request;
            request.strategy = parse_strategy(req.body);
            request.include_greeks = json_get_bool(req.body, "include_greeks");
            
            // Parse timestamps array
            auto ts_pos = req.body.find("\"timestamps\"");
            if (ts_pos != std::string::npos) {
                auto arr_start = req.body.find('[', ts_pos);
                auto arr_end = req.body.find(']', arr_start);
                if (arr_start != std::string::npos && arr_end != std::string::npos) {
                    std::string ts_str = req.body.substr(arr_start + 1, arr_end - arr_start - 1);
                    
                    size_t pos = 0;
                    while (pos < ts_str.length()) {
                        size_t end = ts_str.find(',', pos);
                        if (end == std::string::npos) end = ts_str.length();
                        
                        std::string ts_val = ts_str.substr(pos, end - pos);
                        int64_t ts = parse_timestamp(ts_val);
                        if (ts > 0) {
                            request.timestamps.push_back(core::Timestamp(ts));
                        }
                        pos = end + 1;
                    }
                }
            }
            
            if (request.timestamps.empty()) {
                res.status = 400;
                res.set_json("{\"error\": \"timestamps array required\"}");
                return;
            }
            
            auto result = get_replay_service().batch_historical_payoff(request);
            
            ReplayJsonBuilder json;
            json.start_object()
                .key("results").start_array();
            
            for (size_t i = 0; i < result.results.size(); ++i) {
                if (i > 0) json.next();
                const auto& r = result.results[i];
                json.start_object()
                    .key("timestamp").value(r.timestamp.count())
                    .key("pnl").value(r.pnl);
                
                if (request.include_greeks) {
                    json.key("greeks").start_object()
                        .key("delta").value(r.greeks.delta)
                        .key("gamma").value(r.greeks.gamma)
                        .key("theta").value(r.greeks.theta)
                        .key("vega").value(r.greeks.vega)
                    .end_object();
                }
                
                json.end_object();
            }
            
            json.end_array().end_object();
            res.set_json(json.str());
        });
    }
};

// ============================================================================
// External Setup Function
// ============================================================================

void setup_replay_routes(http::Server& server) {
    ReplayApi api;
    api.setup_routes(server);
}

} // namespace payoff::api