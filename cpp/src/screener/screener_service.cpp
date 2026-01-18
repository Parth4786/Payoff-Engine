/**
 * @file screener_service.cpp
 * @brief Market screener implementation
 * 
 * Provides macro-to-micro market analysis:
 * - All instruments at a timestamp (screener view)
 * - Option chain visualization
 * - IV surface generation
 * - Drill-down replay with streaming
 */

#include "screener/screener_service.hpp"
#include "core/clickhouse_http.hpp"
#include "core/config.hpp"
#include "payoff/pricing.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace payoff::screener {

using namespace payoff::core;

// ============================================================================
// Implementation
// ============================================================================

struct ScreenerService::Impl {
    std::shared_ptr<MarketDataSource> data_source;
    std::shared_ptr<InstrumentManager> instrument_manager;
    ClickHouseHttpConfig ch_config;
    std::string table = "market_data";
    
    // Cache for IV percentile calculation
    std::unordered_map<uint32_t, std::vector<double>> iv_history_cache;
    std::mutex cache_mutex;
    
    // ========================================================================
    // Helpers
        static int64_t parse_expiry_ms(const std::string& s) {
            if (s.empty()) return 0;

            // Some deployments store expiry as a unix-ms integer.
            try {
                size_t idx = 0;
                int64_t v = std::stoll(s, &idx);
                if (idx == s.size()) return v;
            } catch (...) {
            }

            // Common ClickHouse shape: Date or DateTime as string.
            // We parse YYYY-MM-DD and ignore time-of-day.
            if (s.size() >= 10 && s[4] == '-' && s[7] == '-') {
                try {
                    int year = std::stoi(s.substr(0, 4));
                    int month = std::stoi(s.substr(5, 2));
                    int day = std::stoi(s.substr(8, 2));

                    std::tm tm{};
                    tm.tm_year = year - 1900;
                    tm.tm_mon = month - 1;
                    tm.tm_mday = day;
                    tm.tm_hour = 0;
                    tm.tm_min = 0;
                    tm.tm_sec = 0;

    #ifdef _WIN32
                    time_t utc_seconds = _mkgmtime(&tm);
    #else
                    time_t utc_seconds = timegm(&tm);
    #endif
                    if (utc_seconds <= 0) return 0;
                    return static_cast<int64_t>(utc_seconds) * 1000;
                } catch (...) {
                    return 0;
                }
            }

            return 0;
        }
    // ========================================================================
    
    std::string timestamp_to_sql(Timestamp ts) {
        // Convert milliseconds to SQL datetime string
        int64_t ms = ts.count();
        time_t seconds = static_cast<time_t>(ms / 1000);
        int millis = static_cast<int>(ms % 1000);
        
        struct tm* utc = gmtime(&seconds);
        if (!utc) return "";
        
        // Add 5:30 for IST
        utc->tm_hour += 5;
        utc->tm_min += 30;
        mktime(utc);
        
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", utc);
        return std::string(buf);
    }
    
    std::string timestamp_to_date_sql(Timestamp ts) {
        // Convert milliseconds to SQL date string (for Date columns)
        int64_t ms = ts.count();
        time_t seconds = static_cast<time_t>(ms / 1000);
        
        struct tm* utc = gmtime(&seconds);
        if (!utc) return "";
        
        // Add 5:30 for IST
        utc->tm_hour += 5;
        utc->tm_min += 30;
        mktime(utc);
        
        char buf[16];
        strftime(buf, sizeof(buf), "%Y-%m-%d", utc);
        return std::string(buf);
    }
    
    double calculate_iv(double price, double spot, double strike, 
                        double dte_years, engine::OptionType type) {
        if (dte_years <= 0 || price <= 0) return 0.0;
        
        // Newton-Raphson IV calculation
        double iv = 0.25;  // Initial guess
        double rf = 0.07;  // Risk-free rate
        
        for (int i = 0; i < 20; ++i) {
            engine::PricingParams params;
            params.spot = spot;
            params.strike = strike;
            params.time_to_expiry = dte_years;
            params.volatility = iv;
            params.risk_free_rate = rf;
            
            double model_price = engine::bs_price(params, type);
            double vega = engine::calculate_greeks(params, type).vega;
            
            if (std::abs(vega) < 0.001) break;
            
            double diff = model_price - price;
            if (std::abs(diff) < 0.01) break;
            
            iv -= diff / vega;
            iv = std::max(0.01, std::min(3.0, iv));  // Clamp
        }
        
        return iv;
    }
    
    engine::Greeks calculate_greeks(double spot, double strike, double dte_years,
                                    double iv, engine::OptionType type) {
        if (dte_years <= 0 || iv <= 0) return {};
        
        engine::PricingParams params;
        params.spot = spot;
        params.strike = strike;
        params.time_to_expiry = dte_years;
        params.volatility = iv;
        params.risk_free_rate = 0.07;
        
        return engine::calculate_greeks(params, type);
    }
    
    double calculate_iv_percentile(uint32_t instrument_id, double current_iv) {
        // Simplified - return 50 for now
        // TODO: Calculate actual percentile from historical IV
        return 50.0;
    }
    
    std::string extract_underlying(const std::string& tradingsymbol) {
        // Extract underlying from symbol like "NIFTY25JAN26300CE"
        if (tradingsymbol.find("NIFTY") == 0) return "NIFTY";
        if (tradingsymbol.find("BANKNIFTY") == 0) return "BANKNIFTY";
        if (tradingsymbol.find("FINNIFTY") == 0) return "FINNIFTY";
        if (tradingsymbol.find("MIDCPNIFTY") == 0) return "MIDCPNIFTY";
        return tradingsymbol.substr(0, std::min(size_t(10), tradingsymbol.length()));
    }

    static std::string normalize_exchange_string(std::string s) {
        if (s == "1") return "NSE";
        if (s == "2") return "NFO";
        if (s == "3") return "BSE";
        if (s == "4") return "BFO";
        if (s == "5") return "CDS";
        if (s == "6") return "MCX";
        return s;
    }

    static core::InstrumentType parse_instrument_type_fallback(const std::string& inst_type_str) {
        // Accept both string enums ("CE") and numeric encodings ("3").
        if (inst_type_str == "CE") return core::InstrumentType::CE;
        if (inst_type_str == "PE") return core::InstrumentType::PE;
        if (inst_type_str == "FUT") return core::InstrumentType::FUT;
        if (inst_type_str == "EQ") return core::InstrumentType::EQ;
        if (!inst_type_str.empty() &&
            std::all_of(inst_type_str.begin(), inst_type_str.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; })) {
            try {
                int v = std::stoi(inst_type_str);
                switch (v) {
                    case 1: return core::InstrumentType::EQ;
                    case 2: return core::InstrumentType::FUT;
                    case 3: return core::InstrumentType::CE;
                    case 4: return core::InstrumentType::PE;
                    default: return core::InstrumentType::Unknown;
                }
            } catch (...) {
                return core::InstrumentType::Unknown;
            }
        }
        return core::InstrumentType::Unknown;
    }

    static int64_t expiry_ymd_to_ms(const std::chrono::year_month_day& ymd) {
        using namespace std::chrono;
        // Midnight UTC on expiry date.
        auto d = sys_days{ymd};
        return duration_cast<milliseconds>(d.time_since_epoch()).count();
    }
    
    // ========================================================================
    // Main Query Functions
    // ========================================================================
    
    MarketScreenerResult query_market_at_timestamp(
        Timestamp timestamp,
        const ScreenerFilter& filter) {
        
        auto start_time = std::chrono::steady_clock::now();
        MarketScreenerResult result;
        result.timestamp = timestamp;
        
        // Build query for instruments at timestamp
        // We want the LATEST snapshot for each instrument BEFORE the timestamp
        std::ostringstream query;
        query << "SELECT "
              << "instrument_id, tradingsymbol, instrument_type, "
              << "last_price, bid_price_0, ask_price_0, "
              << "total_traded_quantity, toInt64(0) as open_interest, "
              << "toInt64(toUnixTimestamp(exchange_timestamp)) * 1000 as ts_ms, "
              << "exchange, expiry, strike, "
              << "open_price, high_price, low_price, close_price "
              << "FROM " << ch_config.database << "." << table << " "
              << "WHERE exchange_timestamp <= '" << timestamp_to_sql(timestamp) << "' "
              << "AND exchange_timestamp >= '" << timestamp_to_sql(timestamp - Timestamp(60000)) << "' ";  // Within 1 minute
        
        // Apply exchange filter (supports ClickHouse exchange stored as numeric or string)
        if (!filter.exchanges.empty()) {
            query << "AND toString(exchange) IN (";
            for (size_t i = 0; i < filter.exchanges.size(); ++i) {
                if (i > 0) query << ",";
                const auto& ex = filter.exchanges[i];
                if (ex == "NSE") {
                    query << "'NSE','1'";
                } else if (ex == "NFO") {
                    query << "'NFO','2'";
                } else {
                    query << "'" << ex << "'";
                }
            }
            query << ") ";
        }
        
        // Apply underlying filter
        // ClickHouse rows may not contain tradingsymbol; prefer filtering by instrument_id
        // using InstrumentManager when available.
        if (!filter.underlyings.empty() && instrument_manager && !instrument_manager->empty()) {
            std::unordered_set<uint32_t> ids;
            for (const auto& u : filter.underlyings) {
                for (const auto* info : instrument_manager->get_by_underlying(u)) {
                    if (!info) continue;
                    ids.insert(instrument_manager->get_clickhouse_id(info->instrument_token));
                }
            }
            if (!ids.empty()) {
                query << "AND instrument_id IN (";
                bool first = true;
                for (auto id : ids) {
                    if (!first) query << ",";
                    first = false;
                    query << id;
                }
                query << ") ";
            }
        } else if (!filter.underlyings.empty()) {
            // Fallback to tradingsymbol prefix matching.
            query << "AND (";
            for (size_t i = 0; i < filter.underlyings.size(); ++i) {
                if (i > 0) query << " OR ";
                query << "tradingsymbol LIKE '" << filter.underlyings[i] << "%'";
            }
            query << ") ";
        }
        
        // Volume/OI filter
        if (filter.min_volume) {
            query << "AND total_traded_quantity >= " << *filter.min_volume << " ";
        }
        
        query << "ORDER BY exchange_timestamp DESC "
              << "LIMIT " << (filter.limit * 10)  // Fetch more, dedupe later
              << " FORMAT TabSeparated";
        
        auto query_start = std::chrono::steady_clock::now();
        
        // Execute query and collect results
        std::unordered_map<uint32_t, InstrumentSnapshot> latest_snapshots;
        
        clickhouse_query_stream(ch_config, query.str(),
            [&](const std::vector<std::string>& cols) {
                if (cols.size() < 15) return true;
                
                try {
                    uint32_t instrument_id = static_cast<uint32_t>(std::stoul(cols[0]));
                    
                    // Skip if we already have this instrument
                    if (latest_snapshots.count(instrument_id)) return true;
                    
                    InstrumentSnapshot snap;
                    snap.instrument_id = instrument_id;
                    snap.tradingsymbol = cols[1];
                    snap.underlying = extract_underlying(cols[1]);

                    // Exchange (may be numeric or string)
                    snap.exchange = normalize_exchange_string(cols[9]);

                    // Parse instrument type (may be empty / numeric)
                    snap.instrument_type = parse_instrument_type_fallback(cols[2]);
                    if (snap.instrument_type == InstrumentType::CE) {
                        snap.option_type = engine::OptionType::Call;
                    } else if (snap.instrument_type == InstrumentType::PE) {
                        snap.option_type = engine::OptionType::Put;
                    } else if (snap.instrument_type == InstrumentType::Unknown) {
                        // Treat unknown as equity to keep behavior stable.
                        snap.instrument_type = InstrumentType::EQ;
                    }
                    
                    // Prices
                    snap.last_price = std::stod(cols[3]);
                    snap.bid_price = std::stod(cols[4]);
                    snap.ask_price = std::stod(cols[5]);
                    snap.mid_price = (snap.bid_price + snap.ask_price) / 2.0;
                    snap.spread = snap.ask_price - snap.bid_price;
                    snap.spread_pct = (snap.mid_price > 0) ? snap.spread / snap.mid_price : 0;
                    
                    // Volume/OI
                    snap.volume = std::stoll(cols[6]);
                    snap.open_interest = std::stoll(cols[7]);
                    
                    // Timestamp
                    snap.exchange_timestamp = Timestamp(std::stoll(cols[8]));

                    // Parse expiry (may be date string or unix timestamp)
                    snap.expiry_ms = parse_expiry_ms(cols[10]);
                    
                    snap.strike = std::stod(cols[11]);

                    // Enrich using InstrumentManager when ClickHouse metadata is missing or unreliable.
                    if (instrument_manager && !instrument_manager->empty()) {
                        if (const auto* info = instrument_manager->resolve_by_clickhouse_id(instrument_id)) {
                            if (snap.tradingsymbol.empty()) snap.tradingsymbol = info->tradingsymbol;
                            if (snap.underlying.empty()) snap.underlying = info->underlying;
                            snap.exchange = core::exchange_to_string(info->exchange);

                            if (info->instrument_type != core::InstrumentType::Unknown) {
                                snap.instrument_type = info->instrument_type;
                                if (snap.instrument_type == InstrumentType::CE) snap.option_type = engine::OptionType::Call;
                                if (snap.instrument_type == InstrumentType::PE) snap.option_type = engine::OptionType::Put;
                            }

                            if (info->strike) {
                                snap.strike = *info->strike;
                            }
                            if (info->expiry) {
                                // Prefer instrument master expiry if ClickHouse returned 0.
                                auto ms = expiry_ymd_to_ms(*info->expiry);
                                if (snap.expiry_ms <= 0) snap.expiry_ms = ms;
                            }
                        }
                    }
                    
                    latest_snapshots[instrument_id] = snap;
                    
                } catch (const std::exception& e) {
                    // Skip malformed rows
                }
                
                return true;
            });
        
        auto query_end = std::chrono::steady_clock::now();
        result.query_time_ms = std::chrono::duration<double, std::milli>(
            query_end - query_start).count();
        
        // Get spot prices for underlyings (for Greek calculations)
        // Prefer cash/index feed from NSE (exchange=1) over derivatives.
        std::unordered_map<std::string, double> underlying_spots;
        for (auto& [id, snap] : latest_snapshots) {
            if (snap.underlying.empty()) continue;
            if (snap.exchange == "NSE" && snap.instrument_type == InstrumentType::EQ) {
                underlying_spots[snap.underlying] = snap.last_price;
            }
        }
        // Fallback to futures (typically NFO) if NSE spot isn't present.
        for (auto& [id, snap] : latest_snapshots) {
            if (snap.underlying.empty()) continue;
            if (underlying_spots.count(snap.underlying)) continue;
            if (snap.instrument_type == InstrumentType::FUT) {
                underlying_spots[snap.underlying] = snap.last_price;
            }
        }
        
        // Default spot prices if not found
        if (underlying_spots.find("NIFTY") == underlying_spots.end()) {
            underlying_spots["NIFTY"] = 24000.0;  // Fallback
        }
        if (underlying_spots.find("BANKNIFTY") == underlying_spots.end()) {
            underlying_spots["BANKNIFTY"] = 51000.0;
        }
        
        // Calculate Greeks/IV for options
        for (auto& [id, snap] : latest_snapshots) {
            if (snap.instrument_type == InstrumentType::CE ||
                snap.instrument_type == InstrumentType::PE) {
                
                double spot = underlying_spots[snap.underlying];
                
                // Calculate days to expiry
                if (snap.expiry_ms > 0) {
                    double ms_to_expiry = static_cast<double>(snap.expiry_ms - timestamp.count());
                    snap.days_to_expiry = std::max(0.0, ms_to_expiry / (1000.0 * 86400.0));
                } else {
                    snap.days_to_expiry = 7.0;  // Default 7 DTE
                }
                
                double dte_years = snap.days_to_expiry / 365.0;
                
                // Calculate moneyness
                if (snap.strike > 0) {
                    snap.moneyness = (spot - snap.strike) / snap.strike;
                    
                    if (snap.option_type == engine::OptionType::Call) {
                        snap.is_itm = spot > snap.strike;
                        snap.is_otm = spot < snap.strike;
                    } else {
                        snap.is_itm = spot < snap.strike;
                        snap.is_otm = spot > snap.strike;
                    }
                    snap.is_atm = std::abs(snap.moneyness) < 0.01;
                }
                
                // Calculate IV and Greeks
                double price_for_iv = snap.mid_price > 0 ? snap.mid_price : snap.last_price;
                if (price_for_iv > 0 && spot > 0 && snap.strike > 0 && dte_years > 0.001) {
                    snap.implied_volatility = calculate_iv(
                        price_for_iv, spot, snap.strike, dte_years, snap.option_type);
                    
                    if (snap.implied_volatility > 0) {
                        auto greeks = calculate_greeks(
                            spot, snap.strike, dte_years, snap.implied_volatility, snap.option_type);
                        snap.delta = greeks.delta;
                        snap.gamma = greeks.gamma;
                        snap.theta = greeks.theta;
                        snap.vega = greeks.vega;
                    }
                    
                    snap.iv_percentile = calculate_iv_percentile(id, snap.implied_volatility);
                }
            }
        }
        
        auto calc_end = std::chrono::steady_clock::now();
        result.calc_time_ms = std::chrono::duration<double, std::milli>(
            calc_end - query_end).count();
        
        // Apply filters
        for (auto& [id, snap] : latest_snapshots) {
            // Instrument type filter
            bool is_option = (snap.instrument_type == InstrumentType::CE ||
                             snap.instrument_type == InstrumentType::PE);
            bool is_future = (snap.instrument_type == InstrumentType::FUT);
            bool is_equity = (snap.instrument_type == InstrumentType::EQ);
            
            if (is_option && !filter.include_options) continue;
            if (is_future && !filter.include_futures) continue;
            if (is_equity && !filter.include_equities) continue;
            
            // Option type filter
            if (is_option) {
                if (snap.option_type == engine::OptionType::Call && !filter.include_calls) continue;
                if (snap.option_type == engine::OptionType::Put && !filter.include_puts) continue;
            }
            
            // Moneyness filter
            if (filter.only_itm && !snap.is_itm) continue;
            if (filter.only_atm && !snap.is_atm) continue;
            if (filter.only_otm && !snap.is_otm) continue;
            
            if (filter.min_moneyness && snap.moneyness < *filter.min_moneyness) continue;
            if (filter.max_moneyness && snap.moneyness > *filter.max_moneyness) continue;
            
            // DTE filter
            if (filter.min_dte && snap.days_to_expiry < *filter.min_dte) continue;
            if (filter.max_dte && snap.days_to_expiry > *filter.max_dte) continue;
            
            // IV filter
            if (filter.min_iv && snap.implied_volatility < *filter.min_iv) continue;
            if (filter.max_iv && snap.implied_volatility > *filter.max_iv) continue;
            
            // Delta filter
            if (filter.min_delta && snap.delta < *filter.min_delta) continue;
            if (filter.max_delta && snap.delta > *filter.max_delta) continue;
            
            // Spread filter
            if (filter.max_spread_pct && snap.spread_pct > *filter.max_spread_pct) continue;
            
            // Count by type
            if (is_option) result.options_count++;
            else if (is_future) result.futures_count++;
            else result.equities_count++;
            
            result.instruments.push_back(snap);
        }
        
        result.total_instruments = result.instruments.size();
        
        // Sort results
        auto compare = [&filter](const InstrumentSnapshot& a, const InstrumentSnapshot& b) {
            double va = 0, vb = 0;
            switch (filter.sort_by) {
                case ScreenerSortField::InstrumentId:
                    va = a.instrument_id; vb = b.instrument_id; break;
                case ScreenerSortField::LastPrice:
                    va = a.last_price; vb = b.last_price; break;
                case ScreenerSortField::Volume:
                    va = static_cast<double>(a.volume); vb = static_cast<double>(b.volume); break;
                case ScreenerSortField::OpenInterest:
                    va = static_cast<double>(a.open_interest); vb = static_cast<double>(b.open_interest); break;
                case ScreenerSortField::ImpliedVolatility:
                    va = a.implied_volatility; vb = b.implied_volatility; break;
                case ScreenerSortField::IVPercentile:
                    va = a.iv_percentile; vb = b.iv_percentile; break;
                case ScreenerSortField::Delta:
                    va = std::abs(a.delta); vb = std::abs(b.delta); break;
                case ScreenerSortField::Gamma:
                    va = a.gamma; vb = b.gamma; break;
                case ScreenerSortField::Theta:
                    va = std::abs(a.theta); vb = std::abs(b.theta); break;
                case ScreenerSortField::Vega:
                    va = a.vega; vb = b.vega; break;
                case ScreenerSortField::Spread:
                    va = a.spread; vb = b.spread; break;
                case ScreenerSortField::SpreadPct:
                    va = a.spread_pct; vb = b.spread_pct; break;
                case ScreenerSortField::DaysToExpiry:
                    va = a.days_to_expiry; vb = b.days_to_expiry; break;
                case ScreenerSortField::Moneyness:
                    va = std::abs(a.moneyness); vb = std::abs(b.moneyness); break;
                default:
                    va = static_cast<double>(a.volume); vb = static_cast<double>(b.volume); break;
            }
            return (filter.sort_order == SortOrder::Descending) ? (va > vb) : (va < vb);
        };
        
        std::sort(result.instruments.begin(), result.instruments.end(), compare);
        
        // Apply pagination
        if (filter.offset < result.instruments.size()) {
            auto start_it = result.instruments.begin() + filter.offset;
            auto end_it = (filter.offset + filter.limit < result.instruments.size())
                ? result.instruments.begin() + filter.offset + filter.limit
                : result.instruments.end();
            std::vector<InstrumentSnapshot> paginated(start_it, end_it);
            result.instruments = std::move(paginated);
        } else {
            result.instruments.clear();
        }
        
        // Build underlying summaries
        std::unordered_map<std::string, UnderlyingSnapshot> underlying_map;
        for (const auto& [id, snap] : latest_snapshots) {
            auto& u = underlying_map[snap.underlying];
            u.symbol = snap.underlying;
            
            if (snap.instrument_type == InstrumentType::CE) {
                u.total_calls++;
                u.total_call_oi += snap.open_interest;
            } else if (snap.instrument_type == InstrumentType::PE) {
                u.total_puts++;
                u.total_put_oi += snap.open_interest;
            } else if (snap.instrument_type == InstrumentType::EQ ||
                       snap.instrument_type == InstrumentType::FUT) {
                u.spot_price = snap.last_price;
                u.volume += snap.volume;
            }
        }
        
        for (auto& [sym, u] : underlying_map) {
            if (u.total_call_oi > 0) {
                u.pcr_oi = u.total_put_oi / u.total_call_oi;
            }
            result.underlyings.push_back(u);
        }
        
        return result;
    }
    
    ReplayResult execute_replay(const ReplayRequest& request) {
        ReplayResult result;
        result.request = request;
        
        if (request.instrument_ids.empty()) {
            return result;
        }
        
        // Build query
        std::ostringstream query;
        query << "SELECT "
              << "instrument_id, tradingsymbol, "
              << "toInt64(toUnixTimestamp(exchange_timestamp)) * 1000 as ts_ms, "
              << "last_price, bid_price_0, ask_price_0, "
              << "total_traded_quantity, toInt64(0) as open_interest "
              << "FROM " << ch_config.database << "." << table << " "
              << "WHERE exchange_timestamp >= '" << timestamp_to_sql(request.start) << "' "
              << "AND exchange_timestamp <= '" << timestamp_to_sql(request.end) << "' "
              << "AND instrument_id IN (";
        
        for (size_t i = 0; i < request.instrument_ids.size(); ++i) {
            if (i > 0) query << ",";
            query << request.instrument_ids[i];
        }
        query << ") ORDER BY exchange_timestamp ASC "
              << "FORMAT TabSeparated";
        
        // Track first prices for P&L calculation
        std::unordered_map<uint32_t, double> first_prices;
        Timestamp last_sampled(0);
        
        clickhouse_query_stream(ch_config, query.str(),
            [&](const std::vector<std::string>& cols) {
                if (cols.size() < 8) return true;
                
                try {
                    uint32_t instrument_id = static_cast<uint32_t>(std::stoul(cols[0]));
                    Timestamp ts(std::stoll(cols[2]));
                    double price = std::stod(cols[3]);
                    
                    // Initialize first price
                    if (first_prices.find(instrument_id) == first_prices.end()) {
                        first_prices[instrument_id] = price;
                        result.start_price = price;
                    }
                    
                    // Sample at intervals
                    bool should_sample = (request.interval_ms <= 0) ||
                        ((ts - last_sampled).count() >= request.interval_ms);
                    
                    if (should_sample) {
                        ReplaySnapshot snap;
                        snap.timestamp = ts;
                        snap.instrument_id = instrument_id;
                        snap.tradingsymbol = cols[1];
                        snap.last_price = price;
                        snap.bid_price = std::stod(cols[4]);
                        snap.ask_price = std::stod(cols[5]);
                        snap.volume = std::stoll(cols[6]);
                        snap.oi = std::stoll(cols[7]);
                        
                        // P&L from start
                        double first_price = first_prices[instrument_id];
                        snap.price_change = price - first_price;
                        snap.price_change_pct = (first_price > 0) 
                            ? (snap.price_change / first_price) * 100.0 : 0;
                        
                        // Comparison mode
                        if (request.compare_mode) {
                            snap.prediction_delta = price - request.initial_prediction;
                        }
                        
                        // Track max/min
                        if (result.snapshots.empty() || price > result.max_price) {
                            result.max_price = price;
                        }
                        if (result.snapshots.empty() || price < result.min_price) {
                            result.min_price = price;
                        }
                        
                        result.snapshots.push_back(snap);
                        last_sampled = ts;
                    }
                    
                    result.total_ticks++;
                    result.end_price = price;
                    
                } catch (...) {}
                
                return true;
            });
        
        // Calculate summary
        if (!result.snapshots.empty()) {
            result.total_change = result.end_price - result.start_price;
            result.total_change_pct = (result.start_price > 0)
                ? (result.total_change / result.start_price) * 100.0 : 0;
            result.duration_ms = static_cast<double>(
                (request.end - request.start).count());
            
            // Calculate volatility (std dev of returns)
            if (result.snapshots.size() > 1) {
                std::vector<double> returns;
                for (size_t i = 1; i < result.snapshots.size(); ++i) {
                    double ret = (result.snapshots[i].last_price - 
                                 result.snapshots[i-1].last_price) /
                                result.snapshots[i-1].last_price;
                    returns.push_back(ret);
                }
                
                double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
                double sq_sum = 0;
                for (double r : returns) {
                    sq_sum += (r - mean) * (r - mean);
                }
                result.volatility = std::sqrt(sq_sum / returns.size()) * std::sqrt(252.0);
            }
        }
        
        return result;
    }
};

// ============================================================================
// Public Interface
// ============================================================================

ScreenerService::ScreenerService(
    std::shared_ptr<MarketDataSource> data_source,
    std::shared_ptr<InstrumentManager> instrument_manager)
    : impl_(std::make_unique<Impl>()) {
    
    impl_->data_source = std::move(data_source);
    impl_->instrument_manager = std::move(instrument_manager);
    impl_->ch_config = ClickHouseHttpConfig::from_config();
}

ScreenerService::~ScreenerService() = default;

MarketScreenerResult ScreenerService::get_market_at_timestamp(
    Timestamp timestamp,
    const ScreenerFilter& filter) {
    return impl_->query_market_at_timestamp(timestamp, filter);
}

std::vector<Timestamp> ScreenerService::get_available_timestamps(
    Timestamp start, Timestamp end, int sample_interval_seconds) {
    
    std::vector<Timestamp> result;
    
    std::ostringstream query;
    query << "SELECT DISTINCT "
            << "toInt64(toUnixTimestamp(toStartOfMinute(exchange_timestamp))) * 1000 as ts_ms "
          << "FROM " << impl_->ch_config.database << "." << impl_->table << " "
          << "WHERE exchange_timestamp >= '" << impl_->timestamp_to_sql(start) << "' "
          << "AND exchange_timestamp <= '" << impl_->timestamp_to_sql(end) << "' "
          << "ORDER BY ts_ms ASC "
          << "FORMAT TabSeparated";
    
    Timestamp last(0);
    clickhouse_query_stream(impl_->ch_config, query.str(),
        [&](const std::vector<std::string>& cols) {
            if (cols.empty()) return true;

            int64_t ts_value = 0;
            try {
                ts_value = std::stoll(cols[0]);
            } catch (...) {
                return true;  // skip unparsable rows (e.g. \N)
            }

            Timestamp ts(ts_value);
            if ((ts - last).count() >= sample_interval_seconds * 1000) {
                result.push_back(ts);
                last = ts;
            }
            return true;
        });
    
    return result;
}

std::vector<std::string> ScreenerService::get_available_underlyings() {
    std::vector<std::string> result;

    // Prefer a curated list when InstrumentManager is available.
    // This avoids returning tens of thousands of equity underlyings.
    if (impl_->instrument_manager && !impl_->instrument_manager->empty()) {
        static const std::vector<std::string> kCommon = {
            "NIFTY", "BANKNIFTY", "FINNIFTY", "MIDCPNIFTY"
        };
        auto all = impl_->instrument_manager->get_underlyings();
        std::unordered_set<std::string> all_set(all.begin(), all.end());
        for (const auto& u : kCommon) {
            if (all_set.count(u)) result.push_back(u);
        }
        if (!result.empty()) return result;
    }
    
    std::ostringstream query;
    query << "SELECT DISTINCT "
          << "arrayStringConcat(extractAll(tradingsymbol, '^[A-Z]+'), '') as underlying "
          << "FROM " << impl_->ch_config.database << "." << impl_->table << " "
          << "WHERE tradingsymbol != '' "
          << "GROUP BY underlying "
          << "ORDER BY underlying "
          << "FORMAT TabSeparated";
    
    try {
        clickhouse_query_stream(impl_->ch_config, query.str(),
            [&](const std::vector<std::string>& cols) {
                if (!cols.empty() && !cols[0].empty()) {
                    result.push_back(cols[0]);
                }
                return true;
            });
    } catch (const std::exception&) {
        // Swallow ClickHouse errors; fall back below.
    }
    
    // Fallback if query doesn't work well
    if (result.empty()) {
        result = {"NIFTY", "BANKNIFTY", "FINNIFTY", "MIDCPNIFTY"};
    }
    
    return result;
}

std::vector<int64_t> ScreenerService::get_available_expiries(
    const std::string& underlying,
    Timestamp as_of) {
    
    std::vector<int64_t> result;

    // Prefer instrument master data for expiries; ClickHouse market rows may not include expiry/tradingsymbol.
    if (impl_->instrument_manager && !impl_->instrument_manager->empty()) {
        using namespace std::chrono;

        const auto as_of_tp = sys_time<milliseconds>(milliseconds(as_of.count()));
        const auto as_of_day = floor<days>(as_of_tp);

        std::unordered_set<int64_t> seen;
        for (const auto* info : impl_->instrument_manager->get_option_chain(underlying)) {
            if (!info || !info->expiry) continue;
            auto ms = Impl::expiry_ymd_to_ms(*info->expiry);
            if (ms <= 0) continue;
            if (sys_days{*info->expiry} <= as_of_day) continue;
            seen.insert(ms);
        }
        if (!seen.empty()) {
            result.assign(seen.begin(), seen.end());
            std::sort(result.begin(), result.end());
            if (result.size() > 20) result.resize(20);
            return result;
        }
    }
    
    std::ostringstream query;
    query << "SELECT DISTINCT expiry "
          << "FROM " << impl_->ch_config.database << "." << impl_->table << " "
          << "WHERE tradingsymbol LIKE '" << underlying << "%' "
          << "AND expiry > '" << impl_->timestamp_to_date_sql(as_of) << "' "
          << "ORDER BY expiry "
          << "LIMIT 20 "
          << "FORMAT TabSeparated";
    
    try {
        clickhouse_query_stream(impl_->ch_config, query.str(),
            [&](const std::vector<std::string>& cols) {
                if (!cols.empty()) {
                    auto expiry_ms = Impl::parse_expiry_ms(cols[0]);
                    if (expiry_ms > 0) {
                        result.push_back(expiry_ms);
                    }
                }
                return true;
            });
    } catch (const std::exception&) {
        // ClickHouse down and instrument master didn't have expiries.
        // Return empty rather than throwing.
    }
    
    return result;
}

ReplayResult ScreenerService::replay_instruments(const ReplayRequest& request) {
    return impl_->execute_replay(request);
}

void ScreenerService::stream_replay(
    const ReplayRequest& request,
    ReplayStreamCallback callback,
    ScreenerProgressCallback progress_callback) {
    
    if (!callback) return;
    
    auto total_duration = (request.end - request.start).count();
    
    // Build query
    std::ostringstream query;
    query << "SELECT "
          << "instrument_id, tradingsymbol, "
                    << "toInt64(toUnixTimestamp(exchange_timestamp)) * 1000 as ts_ms, "
          << "last_price, bid_price_0, ask_price_0, "
            << "total_traded_quantity, toInt64(0) as open_interest "
          << "FROM " << impl_->ch_config.database << "." << impl_->table << " "
          << "WHERE exchange_timestamp >= '" << impl_->timestamp_to_sql(request.start) << "' "
          << "AND exchange_timestamp <= '" << impl_->timestamp_to_sql(request.end) << "' "
          << "AND instrument_id IN (";
    
    for (size_t i = 0; i < request.instrument_ids.size(); ++i) {
        if (i > 0) query << ",";
        query << request.instrument_ids[i];
    }
    query << ") ORDER BY exchange_timestamp ASC "
          << "FORMAT TabSeparated";
    
    std::unordered_map<uint32_t, double> first_prices;
    Timestamp last_sampled(0);
    
    clickhouse_query_stream(impl_->ch_config, query.str(),
        [&](const std::vector<std::string>& cols) {
            if (cols.size() < 8) return true;
            
            try {
                uint32_t instrument_id = static_cast<uint32_t>(std::stoul(cols[0]));
                Timestamp ts(std::stoll(cols[2]));
                double price = std::stod(cols[3]);
                
                if (first_prices.find(instrument_id) == first_prices.end()) {
                    first_prices[instrument_id] = price;
                }
                
                bool should_sample = (request.interval_ms <= 0) ||
                    ((ts - last_sampled).count() >= request.interval_ms);
                
                if (should_sample) {
                    ReplaySnapshot snap;
                    snap.timestamp = ts;
                    snap.instrument_id = instrument_id;
                    snap.tradingsymbol = cols[1];
                    snap.last_price = price;
                    snap.bid_price = std::stod(cols[4]);
                    snap.ask_price = std::stod(cols[5]);
                    snap.volume = std::stoll(cols[6]);
                    snap.oi = std::stoll(cols[7]);
                    
                    double first_price = first_prices[instrument_id];
                    snap.price_change = price - first_price;
                    snap.price_change_pct = (first_price > 0) 
                        ? (snap.price_change / first_price) * 100.0 : 0;
                    
                    if (request.compare_mode) {
                        snap.prediction_delta = price - request.initial_prediction;
                    }
                    
                    callback(snap);
                    last_sampled = ts;
                    
                    // Progress callback
                    if (progress_callback && total_duration > 0) {
                        double progress = static_cast<double>((ts - request.start).count()) /
                                         static_cast<double>(total_duration) * 100.0;
                        progress_callback(progress);
                    }
                }
                
            } catch (...) {}
            
            return true;
        });
}

ScreenerService::OptionChainResult ScreenerService::get_option_chain(
    const std::string& underlying,
    int64_t expiry_ms,
    Timestamp timestamp) {
    
    OptionChainResult result;
    result.underlying = underlying;
    result.timestamp = timestamp;
    result.expiry_ms = expiry_ms;
    result.source = "clickhouse";
    
    try {
        // Get spot price
        ScreenerFilter filter;
        filter.underlyings = {underlying};
        filter.include_options = false;
        filter.include_futures = true;
        filter.include_equities = true;
        filter.limit = 10;
        
        auto spot_result = get_market_at_timestamp(timestamp, filter);
        if (!spot_result.instruments.empty()) {
            result.spot_price = spot_result.instruments[0].last_price;
        }
        
        // Get options
        filter.include_options = true;
        filter.include_futures = false;
        filter.include_equities = false;
        filter.specific_expiry_ms = expiry_ms;
        filter.limit = 1000;
        
        auto options_result = get_market_at_timestamp(timestamp, filter);
        
        // Group by strike
        std::map<double, OptionChainEntry> chain_map;
        
        for (const auto& snap : options_result.instruments) {
            auto& entry = chain_map[snap.strike];
            entry.strike = snap.strike;
            
            if (snap.option_type == engine::OptionType::Call) {
                entry.call = snap;
            } else {
                entry.put = snap;
            }
        }
        
        // Build chain vector and find ATM
        double min_diff = std::numeric_limits<double>::max();
        for (auto& [strike, entry] : chain_map) {
            entry.net_oi = entry.call.open_interest - entry.put.open_interest;
            entry.net_volume = entry.call.volume - entry.put.volume;
            result.chain.push_back(entry);
            
            double diff = std::abs(strike - result.spot_price);
            if (diff < min_diff) {
                min_diff = diff;
                result.atm_strike = strike;
            }
        }
    } catch (const std::exception& e) {
        // ClickHouse (market data) is unavailable. Fall back to instrument master
        // so the UI can still render strikes and contract identities.
        result.chain.clear();
        result.spot_price = 0.0;
        result.atm_strike = 0.0;
        result.max_pain = 0.0;

        if (impl_->instrument_manager && !impl_->instrument_manager->empty()) {
            result.source = "instrument_master";
            result.warning = std::string("ClickHouse unavailable; returning instrument-master chain without prices: ") + e.what();

            using namespace std::chrono;
            const auto expiry_tp = sys_time<milliseconds>(milliseconds(expiry_ms));
            const auto expiry_day = floor<days>(expiry_tp);
            const auto expiry_ymd = year_month_day{expiry_day};

            std::map<double, OptionChainEntry> chain_map;
            for (const auto* info : impl_->instrument_manager->get_option_chain(underlying)) {
                if (!info || !info->expiry || !info->strike) continue;
                if (sys_days{*info->expiry} != expiry_day) continue;

                InstrumentSnapshot snap;
                snap.instrument_id = impl_->instrument_manager->get_clickhouse_id(info->instrument_token);
                snap.tradingsymbol = info->tradingsymbol;
                snap.underlying = info->underlying;
                snap.exchange = core::exchange_to_string(info->exchange);
                snap.instrument_type = info->instrument_type;
                snap.strike = *info->strike;
                snap.expiry_ms = Impl::expiry_ymd_to_ms(*info->expiry);
                snap.exchange_timestamp = timestamp;
                if (snap.instrument_type == InstrumentType::CE) {
                    snap.option_type = engine::OptionType::Call;
                } else if (snap.instrument_type == InstrumentType::PE) {
                    snap.option_type = engine::OptionType::Put;
                }

                auto& entry = chain_map[snap.strike];
                entry.strike = snap.strike;
                if (snap.option_type == engine::OptionType::Call) entry.call = snap;
                else entry.put = snap;
            }

            for (auto& [strike, entry] : chain_map) {
                entry.net_oi = entry.call.open_interest - entry.put.open_interest;
                entry.net_volume = entry.call.volume - entry.put.volume;
                result.chain.push_back(entry);
            }

            if (!result.chain.empty()) {
                result.atm_strike = result.chain[result.chain.size() / 2].strike;
            }
            (void)expiry_ymd;
        } else {
            result.source = "none";
            result.warning = std::string("ClickHouse unavailable and instrument master not loaded: ") + e.what();
        }
    }
    
    return result;
}

ScreenerService::IVSurfaceResult ScreenerService::get_iv_surface(
    const std::string& underlying,
    Timestamp timestamp) {
    
    IVSurfaceResult result;
    result.underlying = underlying;
    result.timestamp = timestamp;
    
    // Get all options for underlying
    ScreenerFilter filter;
    filter.underlyings = {underlying};
    filter.include_options = true;
    filter.include_futures = false;
    filter.include_equities = false;
    filter.min_volume = 100;
    filter.limit = 5000;
    
    auto options_result = get_market_at_timestamp(timestamp, filter);
    
    if (!options_result.underlyings.empty()) {
        result.spot_price = options_result.underlyings[0].spot_price;
    }
    
    for (const auto& snap : options_result.instruments) {
        if (snap.implied_volatility > 0 && snap.days_to_expiry > 0) {
            IVSurfacePoint point;
            point.strike = snap.strike;
            point.dte = snap.days_to_expiry;
            point.iv = snap.implied_volatility;
            result.surface.push_back(point);
        }
    }
    
    return result;
}

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<ScreenerService> create_screener_service() {
    auto data_source = create_clickhouse_source_from_config();
    auto instrument_manager = std::make_shared<InstrumentManager>();
    instrument_manager->load_with_fallback();
    
    return std::make_unique<ScreenerService>(
        std::move(data_source),
        std::move(instrument_manager));
}

} // namespace payoff::screener
