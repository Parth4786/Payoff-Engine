# Instrument Manager — Deep Dive

> Detailed technical documentation for translating the Python InstrumentService to C++.

---

## Overview

The Instrument Manager is the most complex piece to translate. It handles:

1. **Multi-tier caching** (memory → disk → API)
2. **Symbol building** with exchange-specific formats
3. **Fuzzy search** across 30,000+ instruments
4. **Bulk resolution** for CSV imports

---

## 1. Data Model

### Python Source (`service.py:43-62`)

```python
@dataclass
class Instrument:
    instrument_token: int
    exchange_token: int
    tradingsymbol: str
    name: str
    exchange: str
    segment: str
    instrument_type: str
    lot_size: int
    tick_size: float
    expiry: str | None = None
    strike: float | None = None
```

### C++ Translation

```cpp
#pragma once
#include <string>
#include <optional>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace deskmetrics::domain::instruments {

struct Instrument {
    uint32_t instrument_token{0};
    uint32_t exchange_token{0};
    std::string tradingsymbol;
    std::string name;
    std::string exchange;           // "NSE", "NFO", "BSE", "BFO", "CDS", "MCX"
    std::string segment;            // "NFO-FUT", "NFO-OPT", "NSE", "INDICES"
    std::string instrument_type;    // "FUT", "CE", "PE", "EQ"
    uint16_t lot_size{1};
    double tick_size{0.05};
    std::optional<std::string> expiry;  // "YYYY-MM-DD" format
    std::optional<double> strike;
    
    // Computed field for search (lowercase concat of symbol + name + exchange)
    std::string search_text;
    
    // Equality for deduplication
    bool operator==(const Instrument& other) const {
        return instrument_token == other.instrument_token;
    }
};

// JSON serialization (nlohmann/json)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Instrument,
    instrument_token, exchange_token, tradingsymbol, name,
    exchange, segment, instrument_type, lot_size, tick_size,
    expiry, strike
)

} // namespace deskmetrics::domain::instruments

// Hash for unordered containers
namespace std {
    template<>
    struct hash<deskmetrics::domain::instruments::Instrument> {
        size_t operator()(const deskmetrics::domain::instruments::Instrument& i) const {
            return hash<uint32_t>{}(i.instrument_token);
        }
    };
}
```

---

## 2. Constants

### Python Source (`service.py:20-30`)

```python
EXCHANGES = ["NSE", "NFO", "BSE", "BFO", "CDS", "MCX"]
MONTHS = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]
WEEKLY_EXPIRY_SYMBOLS = {"NIFTY", "BANKNIFTY", "FINNIFTY", "MIDCPNIFTY", "SENSEX", "BANKEX"}
CACHE_EXPIRY_SECONDS = 24 * 60 * 60  # 24 hours
```

### C++ Translation

```cpp
#pragma once
#include <array>
#include <unordered_set>
#include <string>
#include <string_view>

namespace deskmetrics::domain::instruments::constants {

// Supported exchanges
inline constexpr std::array<std::string_view, 6> EXCHANGES = {
    "NSE", "NFO", "BSE", "BFO", "CDS", "MCX"
};

// Month abbreviations (index 0 = January)
inline constexpr std::array<std::string_view, 12> MONTHS = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

// Indices with weekly expiry (use YY+M+DD format instead of YY+MMM)
inline const std::unordered_set<std::string> WEEKLY_EXPIRY_SYMBOLS = {
    "NIFTY", "BANKNIFTY", "FINNIFTY", "MIDCPNIFTY", "SENSEX", "BANKEX"
};

// Cache expiry
inline constexpr int64_t CACHE_EXPIRY_SECONDS = 24 * 60 * 60;

// Lot sizes (approximate, for fallback)
inline constexpr int LOT_SIZE_NIFTY = 75;
inline constexpr int LOT_SIZE_BANKNIFTY = 35;
inline constexpr int LOT_SIZE_FINNIFTY = 75;
inline constexpr int LOT_SIZE_MIDCPNIFTY = 75;
inline constexpr int LOT_SIZE_DEFAULT = 50;

} // namespace
```

---

## 3. Symbol Building (THE HARD PART)

### Python Source (`service.py:303-395`)

This is the most complex function. It converts user-friendly inputs into Kite's specific symbol format.

### Format Rules

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        SYMBOL FORMAT RULES                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  WEEKLY OPTIONS (Indices only - NIFTY, BANKNIFTY, FINNIFTY, etc.)      │
│  ────────────────────────────────────────────────────────────────       │
│  Format: {SYMBOL}{YY}{M}{DD}{STRIKE}{TYPE}                             │
│                                                                         │
│  Month codes:                                                           │
│    Jan=1, Feb=2, ... Sep=9, Oct=O, Nov=N, Dec=D                        │
│                                                                         │
│  Examples:                                                              │
│    NIFTY2510224000CE  → NIFTY, 2025, Jan(1), 02, 24000, CE             │
│    BANKNIFTY25O0948000PE → BANKNIFTY, 2025, Oct(O), 09, 48000, PE      │
│    NIFTY25N1323500CE  → NIFTY, 2025, Nov(N), 13, 23500, CE             │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  MONTHLY OPTIONS (All stocks + indices monthly)                        │
│  ────────────────────────────────────────────────────────────────       │
│  Format: {SYMBOL}{YY}{MMM}{STRIKE}{TYPE}                               │
│                                                                         │
│  Examples:                                                              │
│    NIFTY26JAN26000CE  → NIFTY, 2026, JAN, 26000, CE                    │
│    RELIANCE25MAY1410CE → RELIANCE, 2025, MAY, 1410, CE                 │
│    TATAMOTORS25JUN850PE → TATAMOTORS, 2025, JUN, 850, PE               │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  FUTURES (All)                                                         │
│  ────────────────────────────────────────────────────────────────       │
│  Format: {SYMBOL}{YY}{MMM}FUT                                          │
│                                                                         │
│  Examples:                                                              │
│    NIFTY25MAYFUT      → NIFTY May 2025 futures                         │
│    RELIANCE25JUNFUT   → RELIANCE June 2025 futures                     │
│    BANKNIFTY26JANFUT  → BANKNIFTY January 2026 futures                 │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### C++ Implementation

```cpp
#pragma once
#include <string>
#include <optional>
#include <date/date.h>  // Howard Hinnant's date library
#include "constants.hpp"

namespace deskmetrics::domain::instruments {

using namespace date;

enum class InstrumentType { FUT, CE, PE, EQ };

// Convert month (1-12) to single-character code for weekly expiry
inline char get_month_code(int month) {
    // From Python service.py:385-395
    if (month >= 1 && month <= 9) {
        return static_cast<char>('0' + month);  // '1' to '9'
    }
    switch (month) {
        case 10: return 'O';
        case 11: return 'N';
        case 12: return 'D';
        default: throw std::invalid_argument("Invalid month: " + std::to_string(month));
    }
}

// Get 3-letter month abbreviation
inline std::string_view get_month_abbrev(int month) {
    if (month < 1 || month > 12) {
        throw std::invalid_argument("Invalid month: " + std::to_string(month));
    }
    return constants::MONTHS[month - 1];
}

// Build trading symbol from components
inline std::optional<std::string> build_tradingsymbol(
    std::string_view underlying,
    const year_month_day& expiry,
    InstrumentType type,
    std::optional<double> strike = std::nullopt,
    bool force_weekly = false,
    bool force_monthly = false
) {
    // Uppercase underlying
    std::string symbol(underlying);
    for (auto& c : symbol) c = std::toupper(c);
    
    // Extract date components
    int year = static_cast<int>(expiry.year());
    int month = static_cast<unsigned>(expiry.month());
    int day = static_cast<unsigned>(expiry.day());
    
    std::string yy = std::to_string(year % 100);
    if (yy.length() == 1) yy = "0" + yy;  // Pad to 2 digits
    
    // FUTURES: Always use {SYMBOL}{YY}{MMM}FUT
    if (type == InstrumentType::FUT) {
        return symbol + yy + std::string(get_month_abbrev(month)) + "FUT";
    }
    
    // OPTIONS: CE or PE
    if (type == InstrumentType::CE || type == InstrumentType::PE) {
        if (!strike.has_value()) {
            return std::nullopt;  // Strike required for options
        }
        
        // Format strike (remove .0 if whole number)
        std::string strike_str;
        if (*strike == static_cast<int>(*strike)) {
            strike_str = std::to_string(static_cast<int>(*strike));
        } else {
            strike_str = std::to_string(*strike);
            // Remove trailing zeros after decimal
            strike_str.erase(strike_str.find_last_not_of('0') + 1);
            if (strike_str.back() == '.') strike_str.pop_back();
        }
        
        std::string type_str = (type == InstrumentType::CE) ? "CE" : "PE";
        
        // Determine format: weekly vs monthly
        bool is_weekly_eligible = constants::WEEKLY_EXPIRY_SYMBOLS.contains(symbol);
        
        if (force_monthly || (!is_weekly_eligible && !force_weekly)) {
            // Monthly format: {SYMBOL}{YY}{MMM}{STRIKE}{TYPE}
            return symbol + yy + std::string(get_month_abbrev(month)) + strike_str + type_str;
        } else {
            // Weekly format: {SYMBOL}{YY}{M}{DD}{STRIKE}{TYPE}
            char month_code = get_month_code(month);
            std::string dd = (day < 10 ? "0" : "") + std::to_string(day);
            return symbol + yy + month_code + dd + strike_str + type_str;
        }
    }
    
    return std::nullopt;
}

// Try both weekly and monthly formats, return all candidates
inline std::vector<std::string> build_symbol_candidates(
    std::string_view underlying,
    const year_month_day& expiry,
    InstrumentType type,
    std::optional<double> strike = std::nullopt
) {
    std::vector<std::string> candidates;
    
    // Uppercase underlying
    std::string symbol(underlying);
    for (auto& c : symbol) c = std::toupper(c);
    
    bool is_weekly_eligible = constants::WEEKLY_EXPIRY_SYMBOLS.contains(symbol);
    
    if (type == InstrumentType::CE || type == InstrumentType::PE) {
        // For weekly-eligible symbols, try weekly first (more common)
        if (is_weekly_eligible) {
            if (auto s = build_tradingsymbol(underlying, expiry, type, strike, true, false)) {
                candidates.push_back(*s);
            }
        }
        // Always try monthly as fallback
        if (auto s = build_tradingsymbol(underlying, expiry, type, strike, false, true)) {
            candidates.push_back(*s);
        }
    } else {
        // Futures only have one format
        if (auto s = build_tradingsymbol(underlying, expiry, type, strike)) {
            candidates.push_back(*s);
        }
    }
    
    return candidates;
}

} // namespace
```

---

## 4. Caching Strategy

### Python Source (`service.py:87-200`)

The Python implementation uses:
1. Global `_instrument_cache` (InstrumentCache dataclass)
2. Per-exchange CSV files on disk
3. Kite API as source of truth

### C++ Implementation

```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <filesystem>
#include <fstream>
#include "instrument.hpp"
#include "constants.hpp"

namespace deskmetrics::infra::cache {

namespace fs = std::filesystem;
using namespace std::chrono;
using Clock = steady_clock;

class InstrumentCache {
public:
    explicit InstrumentCache(fs::path cache_dir)
        : cache_dir_(std::move(cache_dir))
    {
        fs::create_directories(cache_dir_);
    }
    
    // Check if memory cache is valid
    bool is_valid() const {
        std::shared_lock lock(mutex_);
        if (instruments_.empty()) return false;
        
        auto age = Clock::now() - last_refresh_;
        return age < seconds(constants::CACHE_EXPIRY_SECONDS);
    }
    
    // Get all instruments (from memory cache)
    std::vector<Instrument> get_all() const {
        std::shared_lock lock(mutex_);
        return instruments_;
    }
    
    // Get instruments by exchange
    std::vector<Instrument> get_by_exchange(std::string_view exchange) const {
        std::shared_lock lock(mutex_);
        
        std::vector<Instrument> result;
        for (const auto& inst : instruments_) {
            if (inst.exchange == exchange) {
                result.push_back(inst);
            }
        }
        return result;
    }
    
    // Update memory cache
    void refresh(std::vector<Instrument> instruments) {
        std::unique_lock lock(mutex_);
        instruments_ = std::move(instruments);
        last_refresh_ = Clock::now();
        rebuild_index();
    }
    
    // Load from disk cache
    std::optional<std::vector<Instrument>> load_from_disk(std::string_view exchange) {
        auto path = get_cache_path(exchange);
        if (!fs::exists(path)) return std::nullopt;
        
        // Check age
        auto last_write = fs::last_write_time(path);
        auto age = fs::file_time_type::clock::now() - last_write;
        if (age > seconds(constants::CACHE_EXPIRY_SECONDS)) {
            return std::nullopt;  // Expired
        }
        
        return load_csv(path);
    }
    
    // Save to disk cache
    void save_to_disk(std::string_view exchange, const std::vector<Instrument>& instruments) {
        auto path = get_cache_path(exchange);
        save_csv(path, instruments);
    }
    
    // Lookup by token (fast)
    std::optional<Instrument> find_by_token(uint32_t token) const {
        std::shared_lock lock(mutex_);
        auto it = token_index_.find(token);
        if (it != token_index_.end()) {
            return instruments_[it->second];
        }
        return std::nullopt;
    }
    
    // Lookup by tradingsymbol (fast)
    std::optional<Instrument> find_by_symbol(std::string_view symbol) const {
        std::shared_lock lock(mutex_);
        
        std::string upper(symbol);
        for (auto& c : upper) c = std::toupper(c);
        
        auto it = symbol_index_.find(upper);
        if (it != symbol_index_.end()) {
            return instruments_[it->second];
        }
        return std::nullopt;
    }

private:
    fs::path cache_dir_;
    mutable std::shared_mutex mutex_;
    
    std::vector<Instrument> instruments_;
    Clock::time_point last_refresh_;
    
    // Indexes for fast lookup
    std::unordered_map<uint32_t, size_t> token_index_;        // token → index
    std::unordered_map<std::string, size_t> symbol_index_;    // symbol → index
    
    fs::path get_cache_path(std::string_view exchange) const {
        std::string filename(exchange);
        for (auto& c : filename) c = std::tolower(c);
        return cache_dir_ / (filename + "_instruments.csv");
    }
    
    void rebuild_index() {
        token_index_.clear();
        symbol_index_.clear();
        
        for (size_t i = 0; i < instruments_.size(); ++i) {
            token_index_[instruments_[i].instrument_token] = i;
            symbol_index_[instruments_[i].tradingsymbol] = i;
        }
    }
    
    // CSV parsing (use fast-cpp-csv-parser or similar)
    std::vector<Instrument> load_csv(const fs::path& path);
    void save_csv(const fs::path& path, const std::vector<Instrument>& instruments);
};

} // namespace
```

---

## 5. Fuzzy Search

### Python Source (`service.py:203-300`)

The Python search:
1. Normalizes query to lowercase tokens
2. Handles special patterns (month names, CE/PE, strike numbers)
3. Uses pandas vectorized string contains
4. Scores results (prioritize NFO/BFO, deprioritize INDICES)

### C++ Implementation

```cpp
#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include "instrument.hpp"
#include "constants.hpp"

namespace deskmetrics::domain::instruments {

struct SearchOptions {
    std::optional<std::string> exchange;
    std::optional<std::string> segment;
    std::optional<std::string> instrument_type;
    size_t limit = 50;
};

// Tokenize and lowercase
inline std::vector<std::string> tokenize(std::string_view query) {
    std::vector<std::string> tokens;
    std::string current;
    
    for (char c : query) {
        if (std::isspace(c)) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += std::tolower(c);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    
    return tokens;
}

// Check if token is a month name
inline bool is_month_token(std::string_view token) {
    for (const auto& m : constants::MONTHS) {
        std::string lower(m);
        for (auto& c : lower) c = std::tolower(c);
        if (lower == token) return true;
    }
    return false;
}

// Check if token looks like a strike price (3+ digits)
inline bool is_strike_token(std::string_view token) {
    if (token.length() < 3) return false;
    return std::all_of(token.begin(), token.end(), ::isdigit);
}

// Search instruments
inline std::vector<Instrument> search(
    const std::vector<Instrument>& instruments,
    std::string_view query,
    const SearchOptions& options = {}
) {
    auto tokens = tokenize(query);
    if (tokens.empty()) return {};
    
    // Detect hints
    bool has_fno_hint = std::any_of(tokens.begin(), tokens.end(), [](const auto& t) {
        return t == "fut" || t == "future" || t == "futures" ||
               t == "ce" || t == "pe" || t == "opt" || t == "option" || t == "options";
    });
    
    bool has_month_hint = std::any_of(tokens.begin(), tokens.end(), is_month_token);
    bool has_strike_hint = std::any_of(tokens.begin(), tokens.end(), is_strike_token);
    
    // Score and filter
    struct ScoredInstrument {
        const Instrument* inst;
        int score;
    };
    
    std::vector<ScoredInstrument> results;
    results.reserve(instruments.size() / 10);  // Rough estimate
    
    for (const auto& inst : instruments) {
        // Apply filters
        if (options.exchange && inst.exchange != *options.exchange) continue;
        if (options.segment && inst.segment != *options.segment) continue;
        if (options.instrument_type && inst.instrument_type != *options.instrument_type) continue;
        
        // Check if all tokens match
        bool matches = true;
        for (const auto& token : tokens) {
            bool token_matched = false;
            
            // Special handling for CE/PE
            if (token == "ce" || token == "pe") {
                std::string type_lower = inst.instrument_type;
                for (auto& c : type_lower) c = std::tolower(c);
                token_matched = (type_lower == token);
            }
            // Special handling for FUT
            else if (token == "fut" || token == "future" || token == "futures") {
                token_matched = (inst.instrument_type == "FUT");
            }
            // Month name
            else if (is_month_token(token)) {
                std::string search_upper(token);
                for (auto& c : search_upper) c = std::toupper(c);
                token_matched = (inst.tradingsymbol.find(search_upper) != std::string::npos);
            }
            // Strike price
            else if (is_strike_token(token)) {
                if (inst.strike) {
                    std::string strike_str = std::to_string(static_cast<int>(*inst.strike));
                    token_matched = (strike_str.find(token) != std::string::npos);
                }
            }
            // General search
            else {
                token_matched = (inst.search_text.find(token) != std::string::npos);
            }
            
            if (!token_matched) {
                matches = false;
                break;
            }
        }
        
        if (!matches) continue;
        
        // Calculate score
        int score = 0;
        
        // Prioritize NFO/BFO (tradeable F&O)
        if (inst.exchange == "NFO" || inst.exchange == "BFO") score += 100;
        else if (inst.exchange == "NSE" || inst.exchange == "BSE") score += 50;
        
        // Deprioritize INDICES
        if (inst.segment == "INDICES") score -= 50;
        
        // Extra boost for F&O if user typed hints
        if (has_fno_hint || has_month_hint || has_strike_hint) {
            if (inst.exchange == "NFO" || inst.exchange == "BFO" || inst.exchange == "CDS") {
                score += 50;
            }
            if (inst.segment == "INDICES") score -= 100;
        }
        
        // Prefer exact tradingsymbol prefix match
        std::string first_token = tokens[0];
        std::string symbol_lower = inst.tradingsymbol;
        for (auto& c : symbol_lower) c = std::tolower(c);
        
        if (symbol_lower.starts_with(first_token)) score += 30;
        
        // Prefer shorter symbols (more likely base instrument)
        score -= static_cast<int>(inst.tradingsymbol.length()) / 10;
        
        results.push_back({&inst, score});
    }
    
    // Sort by score descending
    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        return a.score > b.score;
    });
    
    // Limit and copy
    std::vector<Instrument> output;
    output.reserve(std::min(results.size(), options.limit));
    
    for (size_t i = 0; i < std::min(results.size(), options.limit); ++i) {
        output.push_back(*results[i].inst);
    }
    
    return output;
}

} // namespace
```

---

## 6. Testing

### Test Cases to Implement

```cpp
// tests/test_symbol_builder.cpp

#include <catch2/catch_test_macros.hpp>
#include "symbol_builder.hpp"

using namespace deskmetrics::domain::instruments;
using namespace date;

TEST_CASE("Weekly NIFTY option symbol") {
    auto ymd = 2025_y/January/2;  // Jan 2, 2025
    
    auto symbol = build_tradingsymbol("NIFTY", ymd, InstrumentType::CE, 24000);
    
    REQUIRE(symbol.has_value());
    REQUIRE(*symbol == "NIFTY2510224000CE");
}

TEST_CASE("Monthly NIFTY option symbol") {
    auto ymd = 2026_y/January/29;  // Monthly expiry
    
    auto symbol = build_tradingsymbol("NIFTY", ymd, InstrumentType::CE, 26000, false, true);
    
    REQUIRE(symbol.has_value());
    REQUIRE(*symbol == "NIFTY26JAN26000CE");
}

TEST_CASE("Stock option uses monthly format") {
    auto ymd = 2025_y/May/29;
    
    auto symbol = build_tradingsymbol("RELIANCE", ymd, InstrumentType::CE, 1410);
    
    REQUIRE(symbol.has_value());
    REQUIRE(*symbol == "RELIANCE25MAY1410CE");
}

TEST_CASE("Futures symbol") {
    auto ymd = 2025_y/May/29;
    
    auto symbol = build_tradingsymbol("NIFTY", ymd, InstrumentType::FUT);
    
    REQUIRE(symbol.has_value());
    REQUIRE(*symbol == "NIFTY25MAYFUT");
}

TEST_CASE("Month codes") {
    REQUIRE(get_month_code(1) == '1');
    REQUIRE(get_month_code(9) == '9');
    REQUIRE(get_month_code(10) == 'O');
    REQUIRE(get_month_code(11) == 'N');
    REQUIRE(get_month_code(12) == 'D');
}

TEST_CASE("October weekly option") {
    auto ymd = 2025_y/October/9;
    
    auto symbol = build_tradingsymbol("BANKNIFTY", ymd, InstrumentType::PE, 48000);
    
    REQUIRE(symbol.has_value());
    REQUIRE(*symbol == "BANKNIFTY25O0948000PE");
}
```

---

## Files Summary

| C++ File | Corresponds to Python | Purpose |
|----------|----------------------|---------|
| `instrument.hpp` | `service.py:43-62` | Data structure |
| `constants.hpp` | `service.py:20-30` | Constants |
| `symbol_builder.hpp` | `service.py:303-395` | Symbol construction |
| `instrument_cache.hpp` | `service.py:65-200` | Caching layer |
| `search.hpp` | `service.py:203-300` | Fuzzy search |
| `service.hpp` | `service.py` (full) | Main service class |

---

*Next: [margin_system_deep_dive.md](margin_system_deep_dive.md)*
