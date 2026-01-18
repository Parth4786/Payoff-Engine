/**
 * @file instrument_manager.cpp
 * @brief Implementation of instrument manager
 */

#include "core/instrument_manager.hpp"
#include "core/clickhouse_http.hpp"
#include "core/config.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace payoff::core {

// ============================================================================
// Exchange Parsing
// ============================================================================

Exchange exchange_from_string(std::string_view s) {
    if (s == "NSE") return Exchange::NSE;
    if (s == "NFO") return Exchange::NFO;
    if (s == "BSE") return Exchange::BSE;
    if (s == "BFO") return Exchange::BFO;
    if (s == "CDS") return Exchange::CDS;
    if (s == "MCX") return Exchange::MCX;
    return Exchange::Unknown;
}

// ============================================================================
// InstrumentManager - Loading
// ============================================================================

InstrumentType InstrumentManager::parse_instrument_type(
    std::string_view segment, 
    std::string_view tradingsymbol) {
    
    // Check segment first
    if (segment.find("OPT") != std::string_view::npos) {
        // Check suffix for CE/PE
        if (tradingsymbol.size() >= 2) {
            auto suffix = tradingsymbol.substr(tradingsymbol.size() - 2);
            if (suffix == "CE") return InstrumentType::CE;
            if (suffix == "PE") return InstrumentType::PE;
        }
    }
    
    if (segment.find("FUT") != std::string_view::npos) {
        return InstrumentType::FUT;
    }
    
    if (segment == "NSE" || segment == "BSE") {
        return InstrumentType::EQ;
    }
    
    return InstrumentType::Unknown;
}

size_t InstrumentManager::load_csv(const std::string& filepath, Exchange exchange) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    
    std::string line;
    // Skip header
    if (!std::getline(file, line)) {
        return 0;
    }
    
    // Parse header to find column indices
    std::vector<std::string> headers;
    std::istringstream header_stream(line);
    std::string header;
    while (std::getline(header_stream, header, ',')) {
        headers.push_back(header);
    }
    
    // Find column indices
    auto find_col = [&headers](const std::string& name) -> int {
        auto it = std::find(headers.begin(), headers.end(), name);
        return it != headers.end() ? 
               static_cast<int>(std::distance(headers.begin(), it)) : -1;
    };
    
    int col_instrument_token = find_col("instrument_token");
    int col_exchange_token = find_col("exchange_token");
    int col_tradingsymbol = find_col("tradingsymbol");
    int col_name = find_col("name");
    int col_strike = find_col("strike");
    [[maybe_unused]] int col_expiry = find_col("expiry");
    int col_lot_size = find_col("lot_size");
    int col_tick_size = find_col("tick_size");
    int col_segment = find_col("segment");
    
    size_t loaded = 0;
    [[maybe_unused]] size_t start_idx = instruments_.size();
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> fields;
        std::istringstream line_stream(line);
        std::string field;
        while (std::getline(line_stream, field, ',')) {
            fields.push_back(field);
        }
        
        if (fields.size() < static_cast<size_t>(std::max({
            col_instrument_token, col_exchange_token, col_tradingsymbol})) + 1) {
            continue;
        }
        
        InstrumentInfo info;
        info.exchange = exchange;
        
        // Parse required fields
        if (col_instrument_token >= 0) {
            info.instrument_token = static_cast<uint32_t>(
                std::stoul(fields[static_cast<size_t>(col_instrument_token)]));
        }
        
        if (col_exchange_token >= 0) {
            info.exchange_token = static_cast<uint32_t>(
                std::stoul(fields[static_cast<size_t>(col_exchange_token)]));
        }
        
        if (col_tradingsymbol >= 0) {
            info.tradingsymbol = fields[static_cast<size_t>(col_tradingsymbol)];
        }
        
        // Parse optional fields
        if (col_name >= 0 && col_name < static_cast<int>(fields.size())) {
            info.name = fields[static_cast<size_t>(col_name)];
        }
        
        if (col_strike >= 0 && col_strike < static_cast<int>(fields.size())) {
            try {
                double strike = std::stod(fields[static_cast<size_t>(col_strike)]);
                if (strike > 0) info.strike = strike;
            } catch (...) {}
        }
        
        if (col_lot_size >= 0 && col_lot_size < static_cast<int>(fields.size())) {
            try {
                info.lot_size = std::stoi(fields[static_cast<size_t>(col_lot_size)]);
            } catch (...) {
                info.lot_size = 1;
            }
        }
        
        if (col_tick_size >= 0 && col_tick_size < static_cast<int>(fields.size())) {
            try {
                info.tick_size = std::stod(fields[static_cast<size_t>(col_tick_size)]);
            } catch (...) {
                info.tick_size = 0.05;
            }
        }
        
        if (col_segment >= 0 && col_segment < static_cast<int>(fields.size())) {
            info.segment = fields[static_cast<size_t>(col_segment)];
            info.instrument_type = parse_instrument_type(
                info.segment, info.tradingsymbol);
        }
        
        // Extract underlying from tradingsymbol (simplified)
        // For NIFTY25JAN23000CE -> NIFTY
        if (!info.tradingsymbol.empty()) {
            size_t i = 0;
            while (i < info.tradingsymbol.size() && 
                   !std::isdigit(static_cast<unsigned char>(info.tradingsymbol[i]))) {
                ++i;
            }
            if (i > 0) {
                info.underlying = info.tradingsymbol.substr(0, i);
            }
        }
        
        instruments_.push_back(std::move(info));
        ++loaded;
    }
    
    // Rebuild indices
    build_indices();
    
    return loaded;
}

size_t InstrumentManager::load_directory(const std::string& directory) {
    namespace fs = std::filesystem;
    
    size_t total = 0;
    
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        
        auto filename = entry.path().filename().string();
        if (filename.find("_instruments.csv") == std::string::npos &&
            filename.find("instruments.csv") == std::string::npos) {
            continue;
        }
        
        // Detect exchange from filename
        Exchange ex = Exchange::Unknown;
        if (filename.find("nfo") != std::string::npos) ex = Exchange::NFO;
        else if (filename.find("nse") != std::string::npos) ex = Exchange::NSE;
        else if (filename.find("bse") != std::string::npos) ex = Exchange::BSE;
        else if (filename.find("bfo") != std::string::npos) ex = Exchange::BFO;
        else if (filename.find("cds") != std::string::npos) ex = Exchange::CDS;
        else if (filename.find("mcx") != std::string::npos) ex = Exchange::MCX;
        
        try {
            total += load_csv(entry.path().string(), ex);
        } catch (...) {
            // Skip files that fail to parse
        }
    }
    
    return total;
}

// ============================================================================
// ClickHouse Loading
// ============================================================================

size_t InstrumentManager::load_from_clickhouse(const std::string& database,
                                                const std::string& table) {
    auto ch_cfg = ClickHouseHttpConfig::from_config();
    
    // Override database if provided
    if (!database.empty()) {
        ch_cfg.database = database;
    }
    
    std::string tbl = table.empty() ? "instruments" : table;
    
    // Query all instrument master data
    // Expected schema matches Kite instrument dump format
    std::string query = 
        "SELECT "
        "  instrument_token, "
        "  exchange_token, "
        "  tradingsymbol, "
        "  name, "
        "  exchange, "
        "  segment, "
        "  lot_size, "
        "  tick_size, "
        "  expiry, "
        "  strike "
        "FROM " + ch_cfg.database + "." + tbl + " "
        "FORMAT TabSeparated";
    
    size_t loaded = 0;
    
    try {
        clickhouse_query_stream(ch_cfg, query, [&](const std::vector<std::string>& cols) {
            if (cols.size() < 8) return true;  // Skip invalid rows
            
            InstrumentInfo info;
            
            try {
                // Column 0: instrument_token
                info.instrument_token = static_cast<uint32_t>(std::stoul(cols[0]));
                
                // Column 1: exchange_token (THIS is the instrument_id for matching)
                info.exchange_token = static_cast<uint32_t>(std::stoul(cols[1]));
                
                // Column 2: tradingsymbol
                info.tradingsymbol = cols[2];
                
                // Column 3: name
                if (cols.size() > 3) info.name = cols[3];
                
                // Column 4: exchange
                if (cols.size() > 4) {
                    info.exchange = exchange_from_string(cols[4]);
                }
                
                // Column 5: segment
                if (cols.size() > 5) {
                    info.segment = cols[5];
                    info.instrument_type = parse_instrument_type(
                        info.segment, info.tradingsymbol);
                }
                
                // Column 6: lot_size
                if (cols.size() > 6 && !cols[6].empty()) {
                    try {
                        info.lot_size = std::stoi(cols[6]);
                    } catch (...) {
                        info.lot_size = 1;
                    }
                }
                
                // Column 7: tick_size
                if (cols.size() > 7 && !cols[7].empty()) {
                    try {
                        info.tick_size = std::stod(cols[7]);
                    } catch (...) {
                        info.tick_size = 0.05;
                    }
                }
                
                // Column 8: expiry (may be NULL represented as \N or empty)
                if (cols.size() > 8 && !cols[8].empty() && cols[8] != "\\N") {
                    // Parse YYYY-MM-DD format
                    try {
                        int year, month, day;
                        if (std::sscanf(cols[8].c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
                            info.expiry = std::chrono::year_month_day{
                                std::chrono::year{year},
                                std::chrono::month{static_cast<unsigned>(month)},
                                std::chrono::day{static_cast<unsigned>(day)}
                            };
                        }
                    } catch (...) {}
                }
                
                // Column 9: strike (may be NULL)
                if (cols.size() > 9 && !cols[9].empty() && cols[9] != "\\N") {
                    try {
                        double strike = std::stod(cols[9]);
                        if (strike > 0) info.strike = strike;
                    } catch (...) {}
                }
                
                // Extract underlying from tradingsymbol
                if (!info.tradingsymbol.empty()) {
                    size_t i = 0;
                    while (i < info.tradingsymbol.size() && 
                           !std::isdigit(static_cast<unsigned char>(info.tradingsymbol[i]))) {
                        ++i;
                    }
                    if (i > 0) {
                        info.underlying = info.tradingsymbol.substr(0, i);
                    }
                }
                
                instruments_.push_back(std::move(info));
                ++loaded;
                
            } catch (const std::exception& e) {
                // Skip invalid rows
            }
            
            return true;  // Continue iteration
        });
        
        // Rebuild indices after loading
        if (loaded > 0) {
            build_indices();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to load instruments from ClickHouse: " << e.what() << std::endl;
    }
    
    return loaded;
}

size_t InstrumentManager::load_with_fallback() {
    auto& cfg = config::config();
    if (!cfg.is_loaded()) {
        cfg.load();
    }
    
    size_t total = 0;
    
    // Strategy 1: Try loading from directory
    std::string instrument_dir = cfg.kite_instrument_dir();
    if (!instrument_dir.empty()) {
        try {
            namespace fs = std::filesystem;
            if (fs::exists(instrument_dir) && fs::is_directory(instrument_dir)) {
                total = load_directory(instrument_dir);
                if (total > 0) {
                    std::cout << "[InstrumentManager] Loaded " << total 
                              << " instruments from directory: " << instrument_dir << std::endl;
                    return total;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[InstrumentManager] Directory load failed: " << e.what() << std::endl;
        }
    }
    
    // Strategy 2: Fallback to ClickHouse
    std::cout << "[InstrumentManager] Falling back to ClickHouse instrument dump..." << std::endl;
    try {
        total = load_from_clickhouse();
        if (total > 0) {
            std::cout << "[InstrumentManager] Loaded " << total 
                      << " instruments from ClickHouse" << std::endl;
        } else {
            std::cerr << "[InstrumentManager] WARNING: No instruments loaded!" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[InstrumentManager] ClickHouse load failed: " << e.what() << std::endl;
    }
    
    return total;
}

void InstrumentManager::clear() noexcept {
    instruments_.clear();
    by_exchange_token_.clear();
    by_instrument_token_.clear();
    by_canonical_.clear();
    by_tradingsymbol_.clear();
    by_underlying_.clear();
}

void InstrumentManager::build_indices() {
    by_exchange_token_.clear();
    by_instrument_token_.clear();
    by_canonical_.clear();
    by_tradingsymbol_.clear();
    by_underlying_.clear();
    
    for (size_t i = 0; i < instruments_.size(); ++i) {
        const auto& inst = instruments_[i];
        
        by_exchange_token_[inst.exchange_token] = i;
        by_instrument_token_[inst.instrument_token] = i;
        by_canonical_[inst.canonical_symbol()] = i;
        by_tradingsymbol_.emplace(inst.tradingsymbol, i);
        
        if (!inst.underlying.empty()) {
            by_underlying_.emplace(inst.underlying, i);
        }
    }
}

// ============================================================================
// InstrumentManager - Lookup
// ============================================================================

const InstrumentInfo* InstrumentManager::resolve(uint32_t exchange_token) const noexcept {
    auto it = by_exchange_token_.find(exchange_token);
    if (it == by_exchange_token_.end()) return nullptr;
    return &instruments_[it->second];
}

const InstrumentInfo* InstrumentManager::resolve_by_instrument_token(
    uint32_t instrument_token) const noexcept {
    auto it = by_instrument_token_.find(instrument_token);
    if (it == by_instrument_token_.end()) return nullptr;
    return &instruments_[it->second];
}

const InstrumentInfo* InstrumentManager::resolve_by_canonical(
    std::string_view canonical_symbol) const noexcept {
    auto it = by_canonical_.find(std::string(canonical_symbol));
    if (it == by_canonical_.end()) return nullptr;
    return &instruments_[it->second];
}

std::vector<const InstrumentInfo*> InstrumentManager::resolve_by_tradingsymbol(
    std::string_view tradingsymbol) const {
    std::vector<const InstrumentInfo*> result;
    auto [begin, end] = by_tradingsymbol_.equal_range(std::string(tradingsymbol));
    for (auto it = begin; it != end; ++it) {
        result.push_back(&instruments_[it->second]);
    }
    return result;
}

// ============================================================================
// InstrumentManager - Query
// ============================================================================

std::vector<const InstrumentInfo*> InstrumentManager::get_by_underlying(
    std::string_view underlying) const {
    std::vector<const InstrumentInfo*> result;
    auto [begin, end] = by_underlying_.equal_range(std::string(underlying));
    for (auto it = begin; it != end; ++it) {
        result.push_back(&instruments_[it->second]);
    }
    return result;
}

std::vector<const InstrumentInfo*> InstrumentManager::get_option_chain(
    std::string_view underlying,
    std::optional<std::chrono::year_month_day> expiry) const {
    std::vector<const InstrumentInfo*> result;
    
    auto instruments = get_by_underlying(underlying);
    for (const auto* inst : instruments) {
        if (!inst->is_option()) continue;
        
        if (expiry && inst->expiry != expiry) continue;
        
        result.push_back(inst);
    }
    
    // Sort by strike, then by type (CE before PE)
    std::sort(result.begin(), result.end(),
        [](const InstrumentInfo* a, const InstrumentInfo* b) {
            if (a->strike != b->strike) {
                return a->strike.value_or(0) < b->strike.value_or(0);
            }
            return static_cast<int>(a->instrument_type) < 
                   static_cast<int>(b->instrument_type);
        });
    
    return result;
}

std::vector<const InstrumentInfo*> InstrumentManager::get_futures(
    std::string_view underlying) const {
    std::vector<const InstrumentInfo*> result;
    
    auto instruments = get_by_underlying(underlying);
    for (const auto* inst : instruments) {
        if (inst->is_futures()) {
            result.push_back(inst);
        }
    }
    
    return result;
}

size_t InstrumentManager::count_by_exchange(Exchange ex) const noexcept {
    return static_cast<size_t>(std::count_if(
        instruments_.begin(), instruments_.end(),
        [ex](const InstrumentInfo& i) { return i.exchange == ex; }));
}

std::vector<std::string> InstrumentManager::get_underlyings() const {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    
    for (const auto& inst : instruments_) {
        if (!inst.underlying.empty() && seen.insert(inst.underlying).second) {
            result.push_back(inst.underlying);
        }
    }
    
    std::sort(result.begin(), result.end());
    return result;
}

// ============================================================================
// Global Instance
// ============================================================================

InstrumentManager& get_instrument_manager() {
    static InstrumentManager instance;
    return instance;
}

} // namespace payoff::core
