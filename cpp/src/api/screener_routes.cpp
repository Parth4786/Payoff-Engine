/**
 * @file screener_routes.cpp
 * @brief REST API routes for market screener
 * 
 * Endpoints:
 * - GET  /api/screener/market          - Get market state at timestamp
 * - GET  /api/screener/timestamps      - Get available timestamps
 * - GET  /api/screener/underlyings     - Get available underlyings
 * - GET  /api/screener/expiries        - Get available expiries
 * - GET  /api/screener/chain           - Get option chain
 * - GET  /api/screener/iv-surface      - Get IV surface
 * - POST /api/screener/replay          - Replay instruments
 * 
 * All heavy computation (Greeks, IV, P&L) handled server-side.
 * Frontend just renders the pre-computed results.
 */

#include "api/http_server.hpp"
#include "screener/screener_service.hpp"

#include <iostream>
#include <memory>
#include <sstream>

namespace payoff::api {

// ============================================================================
// JSON Builder (shared)
// ============================================================================

namespace {

class ScreenerJsonBuilder {
public:
    ScreenerJsonBuilder& start_object() { ss_ << "{"; first_ = true; return *this; }
    ScreenerJsonBuilder& end_object() { ss_ << "}"; return *this; }
    ScreenerJsonBuilder& start_array() { ss_ << "["; first_ = true; return *this; }
    ScreenerJsonBuilder& end_array() { ss_ << "]"; return *this; }
    
    ScreenerJsonBuilder& key(const std::string& k) {
        if (!first_) ss_ << ",";
        ss_ << "\"" << k << "\":";
        first_ = false;
        return *this;
    }
    
    ScreenerJsonBuilder& value(const std::string& v) { 
        ss_ << "\"" << escape(v) << "\""; 
        return *this; 
    }
    ScreenerJsonBuilder& value(double v) { 
        ss_ << std::fixed << std::setprecision(4) << v; 
        return *this; 
    }
    ScreenerJsonBuilder& value(int v) { ss_ << v; return *this; }
    ScreenerJsonBuilder& value(int64_t v) { ss_ << v; return *this; }
    ScreenerJsonBuilder& value(uint32_t v) { ss_ << v; return *this; }
    ScreenerJsonBuilder& value(size_t v) { ss_ << v; return *this; }
    ScreenerJsonBuilder& value(bool v) { ss_ << (v ? "true" : "false"); return *this; }
    ScreenerJsonBuilder& null_value() { ss_ << "null"; return *this; }
    ScreenerJsonBuilder& next() { first_ = false; return *this; }
    ScreenerJsonBuilder& raw(const std::string& s) { ss_ << s; return *this; }
    
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

std::vector<std::string> parse_csv(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t start = item.find_first_not_of(" \t");
        size_t end = item.find_last_not_of(" \t");
        if (start != std::string::npos) {
            result.push_back(item.substr(start, end - start + 1));
        }
    }
    return result;
}

std::vector<uint32_t> parse_instrument_ids(const std::string& s) {
    std::vector<uint32_t> result;
    auto parts = parse_csv(s);
    for (const auto& p : parts) {
        try { result.push_back(static_cast<uint32_t>(std::stoul(p))); }
        catch (...) {}
    }
    return result;
}

// Serialize instrument snapshot to JSON
void serialize_instrument(ScreenerJsonBuilder& json, 
                         const screener::InstrumentSnapshot& snap) {
    json.start_object()
        .key("instrument_id").value(snap.instrument_id)
        .key("tradingsymbol").value(snap.tradingsymbol)
        .key("underlying").value(snap.underlying)
        .key("exchange").value(snap.exchange)
        .key("instrument_type").value(static_cast<int>(snap.instrument_type))
        .key("strike").value(snap.strike)
        .key("option_type").value(snap.option_type == engine::OptionType::Call ? "CE" : "PE")
        .key("expiry_ms").value(snap.expiry_ms)
        .key("days_to_expiry").value(snap.days_to_expiry)
        .key("last_price").value(snap.last_price)
        .key("bid_price").value(snap.bid_price)
        .key("ask_price").value(snap.ask_price)
        .key("mid_price").value(snap.mid_price)
        .key("spread").value(snap.spread)
        .key("spread_pct").value(snap.spread_pct)
        .key("volume").value(snap.volume)
        .key("open_interest").value(snap.open_interest)
        .key("oi_change").value(snap.oi_change)
        .key("iv").value(snap.implied_volatility)
        .key("iv_pct").value(snap.implied_volatility * 100.0)
        .key("iv_percentile").value(snap.iv_percentile)
        .key("delta").value(snap.delta)
        .key("gamma").value(snap.gamma)
        .key("theta").value(snap.theta)
        .key("vega").value(snap.vega)
        .key("moneyness").value(snap.moneyness)
        .key("is_itm").value(snap.is_itm)
        .key("is_atm").value(snap.is_atm)
        .key("is_otm").value(snap.is_otm)
        .key("timestamp").value(snap.exchange_timestamp.count())
        .end_object();
}

// Global screener service instance
std::unique_ptr<screener::ScreenerService> g_screener_service;

screener::ScreenerService& get_screener() {
    if (!g_screener_service) {
        g_screener_service = screener::create_screener_service();
    }
    return *g_screener_service;
}

} // anonymous namespace

// ============================================================================
// Screener API Routes
// ============================================================================

class ScreenerApi {
public:
    void setup_routes(http::Server& server) {
        
        // ====================================================================
        // GET /api/screener/market - Main screener endpoint
        // ====================================================================
        // Query params:
        //   timestamp (required): Unix ms timestamp
        //   exchanges: Comma-separated (NFO,NSE,BFO)
        //   underlyings: Comma-separated (NIFTY,BANKNIFTY)
        //   include_options: bool (default: true)
        //   include_futures: bool (default: true)
        //   include_equities: bool (default: false)
        //   include_calls: bool (default: true)
        //   include_puts: bool (default: true)
        //   min_dte, max_dte: int
        //   only_itm, only_atm, only_otm: bool
        //   min_volume, min_oi: int64
        //   min_iv, max_iv: double
        //   max_spread_pct: double
        //   sort_by: string (volume, oi, iv, delta, etc.)
        //   sort_order: asc/desc
        //   offset, limit: int
        // ====================================================================
        server.Get("/api/screener/market", [](const http::Request& req, http::Response& res) {
            // Parse timestamp (required)
            auto ts_str = req.get_param("timestamp", "");
            if (ts_str.empty()) {
                res.status = 400;
                res.set_json("{\"error\": \"timestamp parameter required\"}");
                return;
            }
            
            core::Timestamp timestamp(parse_timestamp(ts_str));
            
            // Build filter
            screener::ScreenerFilter filter;
            
            // Exchanges
            auto exchanges_str = req.get_param("exchanges", "");
            if (!exchanges_str.empty()) {
                filter.exchanges = parse_csv(exchanges_str);
            }
            
            // Underlyings
            auto underlyings_str = req.get_param("underlyings", "");
            if (!underlyings_str.empty()) {
                filter.underlyings = parse_csv(underlyings_str);
            }
            
            // Instrument type flags
            filter.include_options = req.get_param("include_options", "true") == "true";
            filter.include_futures = req.get_param("include_futures", "true") == "true";
            filter.include_equities = req.get_param("include_equities", "false") == "true";
            filter.include_calls = req.get_param("include_calls", "true") == "true";
            filter.include_puts = req.get_param("include_puts", "true") == "true";
            
            // DTE filter
            auto min_dte_str = req.get_param("min_dte", "");
            auto max_dte_str = req.get_param("max_dte", "");
            if (!min_dte_str.empty()) filter.min_dte = std::stoi(min_dte_str);
            if (!max_dte_str.empty()) filter.max_dte = std::stoi(max_dte_str);
            
            // Moneyness filter
            filter.only_itm = req.get_param("only_itm", "false") == "true";
            filter.only_atm = req.get_param("only_atm", "false") == "true";
            filter.only_otm = req.get_param("only_otm", "false") == "true";
            
            // Volume/OI filter
            auto min_vol_str = req.get_param("min_volume", "");
            auto min_oi_str = req.get_param("min_oi", "");
            if (!min_vol_str.empty()) filter.min_volume = std::stoll(min_vol_str);
            if (!min_oi_str.empty()) filter.min_oi = std::stoll(min_oi_str);
            
            // IV filter
            auto min_iv_str = req.get_param("min_iv", "");
            auto max_iv_str = req.get_param("max_iv", "");
            if (!min_iv_str.empty()) filter.min_iv = std::stod(min_iv_str);
            if (!max_iv_str.empty()) filter.max_iv = std::stod(max_iv_str);
            
            // Spread filter
            auto max_spread_str = req.get_param("max_spread_pct", "");
            if (!max_spread_str.empty()) filter.max_spread_pct = std::stod(max_spread_str);
            
            // Sorting
            auto sort_by_str = req.get_param("sort_by", "volume");
            if (sort_by_str == "iv") filter.sort_by = screener::ScreenerSortField::ImpliedVolatility;
            else if (sort_by_str == "delta") filter.sort_by = screener::ScreenerSortField::Delta;
            else if (sort_by_str == "gamma") filter.sort_by = screener::ScreenerSortField::Gamma;
            else if (sort_by_str == "theta") filter.sort_by = screener::ScreenerSortField::Theta;
            else if (sort_by_str == "vega") filter.sort_by = screener::ScreenerSortField::Vega;
            else if (sort_by_str == "oi") filter.sort_by = screener::ScreenerSortField::OpenInterest;
            else if (sort_by_str == "spread") filter.sort_by = screener::ScreenerSortField::SpreadPct;
            else if (sort_by_str == "dte") filter.sort_by = screener::ScreenerSortField::DaysToExpiry;
            else if (sort_by_str == "price") filter.sort_by = screener::ScreenerSortField::LastPrice;
            else filter.sort_by = screener::ScreenerSortField::Volume;
            
            filter.sort_order = (req.get_param("sort_order", "desc") == "asc") 
                ? screener::SortOrder::Ascending : screener::SortOrder::Descending;
            
            // Pagination
            filter.offset = static_cast<size_t>(std::stoi(req.get_param("offset", "0")));
            filter.limit = static_cast<size_t>(std::stoi(req.get_param("limit", "100")));
            
            // Execute query
            auto result = get_screener().get_market_at_timestamp(timestamp, filter);
            
            // Build response
            ScreenerJsonBuilder json;
            json.start_object()
                .key("timestamp").value(result.timestamp.count())
                .key("total_instruments").value(result.total_instruments)
                .key("options_count").value(result.options_count)
                .key("futures_count").value(result.futures_count)
                .key("equities_count").value(result.equities_count)
                .key("query_time_ms").value(result.query_time_ms)
                .key("calc_time_ms").value(result.calc_time_ms);
            
            // Underlyings
            json.key("underlyings").start_array();
            for (size_t i = 0; i < result.underlyings.size(); ++i) {
                if (i > 0) json.next();
                const auto& u = result.underlyings[i];
                json.start_object()
                    .key("symbol").value(u.symbol)
                    .key("spot_price").value(u.spot_price)
                    .key("prev_close").value(u.prev_close)
                    .key("change").value(u.change)
                    .key("change_pct").value(u.change_pct)
                    .key("volume").value(u.volume)
                    .key("atm_iv").value(u.atm_iv)
                    .key("total_calls").value(u.total_calls)
                    .key("total_puts").value(u.total_puts)
                    .key("total_call_oi").value(u.total_call_oi)
                    .key("total_put_oi").value(u.total_put_oi)
                    .key("pcr_oi").value(u.pcr_oi)
                    .key("pcr_volume").value(u.pcr_volume)
                    .end_object();
            }
            json.end_array();
            
            // Instruments
            json.key("instruments").start_array();
            for (size_t i = 0; i < result.instruments.size(); ++i) {
                if (i > 0) json.next();
                serialize_instrument(json, result.instruments[i]);
            }
            json.end_array();
            
            json.end_object();
            res.set_json(json.str());
        });
        
        // ====================================================================
        // GET /api/screener/timestamps - Available timestamps
        // ====================================================================
        server.Get("/api/screener/timestamps", [](const http::Request& req, http::Response& res) {
            auto start_str = req.get_param("start", "");
            auto end_str = req.get_param("end", "");
            int interval = std::stoi(req.get_param("interval", "60"));  // seconds
            
            if (start_str.empty() || end_str.empty()) {
                res.status = 400;
                res.set_json("{\"error\": \"start and end parameters required\"}");
                return;
            }
            
            core::Timestamp start(parse_timestamp(start_str));
            core::Timestamp end(parse_timestamp(end_str));
            
            auto timestamps = get_screener().get_available_timestamps(start, end, interval);
            
            ScreenerJsonBuilder json;
            json.start_object()
                .key("count").value(timestamps.size())
                .key("timestamps").start_array();
            
            for (size_t i = 0; i < timestamps.size(); ++i) {
                if (i > 0) json.next();
                json.value(timestamps[i].count());
            }
            
            json.end_array().end_object();
            res.set_json(json.str());
        });
        
        // ====================================================================
        // GET /api/screener/underlyings - Available underlyings
        // ====================================================================
        server.Get("/api/screener/underlyings", [](const http::Request&, http::Response& res) {
            auto underlyings = get_screener().get_available_underlyings();
            
            ScreenerJsonBuilder json;
            json.start_object()
                .key("underlyings").start_array();
            
            for (size_t i = 0; i < underlyings.size(); ++i) {
                if (i > 0) json.next();
                json.value(underlyings[i]);
            }
            
            json.end_array().end_object();
            res.set_json(json.str());
        });
        
        // ====================================================================
        // GET /api/screener/expiries - Available expiries for underlying
        // ====================================================================
        server.Get("/api/screener/expiries", [](const http::Request& req, http::Response& res) {
            auto underlying = req.get_param("underlying", "NIFTY");
            auto as_of_str = req.get_param("as_of", "");
            
            core::Timestamp as_of(0);
            if (!as_of_str.empty()) {
                as_of = core::Timestamp(parse_timestamp(as_of_str));
            } else {
                // Default to now
                as_of = core::Timestamp(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
            }
            
            auto expiries = get_screener().get_available_expiries(underlying, as_of);
            
            ScreenerJsonBuilder json;
            json.start_object()
                .key("underlying").value(underlying)
                .key("expiries").start_array();
            
            for (size_t i = 0; i < expiries.size(); ++i) {
                if (i > 0) json.next();
                json.value(expiries[i]);
            }
            
            json.end_array().end_object();
            res.set_json(json.str());
        });
        
        // ====================================================================
        // GET /api/screener/chain - Option chain for underlying+expiry
        // ====================================================================
        server.Get("/api/screener/chain", [](const http::Request& req, http::Response& res) {
            auto underlying = req.get_param("underlying", "NIFTY");
            auto expiry_str = req.get_param("expiry", "");
            auto timestamp_str = req.get_param("timestamp", "");
            
            if (expiry_str.empty() || timestamp_str.empty()) {
                res.status = 400;
                res.set_json("{\"error\": \"expiry and timestamp parameters required\"}");
                return;
            }
            
            int64_t expiry_ms = parse_timestamp(expiry_str);
            core::Timestamp timestamp(parse_timestamp(timestamp_str));
            
            auto result = get_screener().get_option_chain(underlying, expiry_ms, timestamp);
            
            ScreenerJsonBuilder json;
            json.start_object()
                .key("underlying").value(result.underlying)
                .key("spot_price").value(result.spot_price)
                .key("expiry_ms").value(result.expiry_ms)
                .key("timestamp").value(result.timestamp.count())
                .key("atm_strike").value(result.atm_strike)
                .key("max_pain").value(result.max_pain)
                .key("chain").start_array();
            
            for (size_t i = 0; i < result.chain.size(); ++i) {
                if (i > 0) json.next();
                const auto& entry = result.chain[i];
                json.start_object()
                    .key("strike").value(entry.strike)
                    .key("net_oi").value(entry.net_oi)
                    .key("net_volume").value(entry.net_volume)
                    .key("call").raw("");  // Start call object
                serialize_instrument(json, entry.call);
                json.key("put").raw("");
                serialize_instrument(json, entry.put);
                json.end_object();
            }
            
            json.end_array().end_object();
            res.set_json(json.str());
        });
        
        // ====================================================================
        // GET /api/screener/iv-surface - IV surface for underlying
        // ====================================================================
        server.Get("/api/screener/iv-surface", [](const http::Request& req, http::Response& res) {
            auto underlying = req.get_param("underlying", "NIFTY");
            auto timestamp_str = req.get_param("timestamp", "");
            
            if (timestamp_str.empty()) {
                res.status = 400;
                res.set_json("{\"error\": \"timestamp parameter required\"}");
                return;
            }
            
            core::Timestamp timestamp(parse_timestamp(timestamp_str));
            
            auto result = get_screener().get_iv_surface(underlying, timestamp);
            
            ScreenerJsonBuilder json;
            json.start_object()
                .key("underlying").value(result.underlying)
                .key("spot_price").value(result.spot_price)
                .key("timestamp").value(result.timestamp.count())
                .key("surface").start_array();
            
            for (size_t i = 0; i < result.surface.size(); ++i) {
                if (i > 0) json.next();
                const auto& pt = result.surface[i];
                json.start_object()
                    .key("strike").value(pt.strike)
                    .key("dte").value(pt.dte)
                    .key("iv").value(pt.iv)
                    .key("iv_pct").value(pt.iv * 100.0)
                    .end_object();
            }
            
            json.end_array().end_object();
            res.set_json(json.str());
        });
        
        // ====================================================================
        // POST /api/screener/replay - Replay instruments over time range
        // ====================================================================
        // Body: {
        //   "instrument_ids": [12345, 67890],
        //   "start": 1705555200000,
        //   "end": 1705560000000,
        //   "interval_ms": 1000,
        //   "include_greeks": true,
        //   "compare_mode": false,
        //   "initial_prediction": 0
        // }
        server.Post("/api/screener/replay", [](const http::Request& req, http::Response& res) {
            // Parse request body (simplified parsing)
            screener::ReplayRequest request;
            
            // Parse instrument_ids from body
            auto ids_start = req.body.find("\"instrument_ids\"");
            if (ids_start != std::string::npos) {
                auto arr_start = req.body.find('[', ids_start);
                auto arr_end = req.body.find(']', arr_start);
                if (arr_start != std::string::npos && arr_end != std::string::npos) {
                    std::string ids_str = req.body.substr(arr_start + 1, arr_end - arr_start - 1);
                    request.instrument_ids = parse_instrument_ids(ids_str);
                }
            }
            
            // Parse timestamps
            auto start_pos = req.body.find("\"start\"");
            if (start_pos != std::string::npos) {
                auto colon = req.body.find(':', start_pos);
                auto comma = req.body.find_first_of(",}", colon);
                std::string val = req.body.substr(colon + 1, comma - colon - 1);
                request.start = core::Timestamp(parse_timestamp(val));
            }
            
            auto end_pos = req.body.find("\"end\"");
            if (end_pos != std::string::npos) {
                auto colon = req.body.find(':', end_pos);
                auto comma = req.body.find_first_of(",}", colon);
                std::string val = req.body.substr(colon + 1, comma - colon - 1);
                request.end = core::Timestamp(parse_timestamp(val));
            }
            
            // Parse interval
            auto interval_pos = req.body.find("\"interval_ms\"");
            if (interval_pos != std::string::npos) {
                auto colon = req.body.find(':', interval_pos);
                auto comma = req.body.find_first_of(",}", colon);
                std::string val = req.body.substr(colon + 1, comma - colon - 1);
                request.interval_ms = std::stoi(val);
            }
            
            // Parse compare_mode
            request.compare_mode = req.body.find("\"compare_mode\":true") != std::string::npos;
            
            // Parse initial_prediction
            auto pred_pos = req.body.find("\"initial_prediction\"");
            if (pred_pos != std::string::npos) {
                auto colon = req.body.find(':', pred_pos);
                auto comma = req.body.find_first_of(",}", colon);
                std::string val = req.body.substr(colon + 1, comma - colon - 1);
                request.initial_prediction = std::stod(val);
            }
            
            if (request.instrument_ids.empty()) {
                res.status = 400;
                res.set_json("{\"error\": \"instrument_ids required\"}");
                return;
            }
            
            // Execute replay
            auto result = get_screener().replay_instruments(request);
            
            // Build response
            ScreenerJsonBuilder json;
            json.start_object()
                .key("total_ticks").value(result.total_ticks)
                .key("snapshot_count").value(result.snapshots.size())
                .key("start_price").value(result.start_price)
                .key("end_price").value(result.end_price)
                .key("total_change").value(result.total_change)
                .key("total_change_pct").value(result.total_change_pct)
                .key("max_price").value(result.max_price)
                .key("min_price").value(result.min_price)
                .key("volatility").value(result.volatility)
                .key("duration_ms").value(result.duration_ms)
                .key("snapshots").start_array();
            
            for (size_t i = 0; i < result.snapshots.size(); ++i) {
                if (i > 0) json.next();
                const auto& snap = result.snapshots[i];
                json.start_object()
                    .key("timestamp").value(snap.timestamp.count())
                    .key("instrument_id").value(snap.instrument_id)
                    .key("tradingsymbol").value(snap.tradingsymbol)
                    .key("last_price").value(snap.last_price)
                    .key("bid_price").value(snap.bid_price)
                    .key("ask_price").value(snap.ask_price)
                    .key("volume").value(snap.volume)
                    .key("oi").value(snap.oi)
                    .key("price_change").value(snap.price_change)
                    .key("price_change_pct").value(snap.price_change_pct);
                
                if (request.compare_mode) {
                    json.key("prediction_delta").value(snap.prediction_delta);
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

void setup_screener_routes(http::Server& server) {
    ScreenerApi api;
    api.setup_routes(server);
}

} // namespace payoff::api
