/**
 * @file instrument_manager.cpp
 * @brief Implementation of instrument manager
 */

#include "core/instrument_manager.hpp"
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
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
