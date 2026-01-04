# C++ Options Platform — Architecture & Implementation Guide

> **Porting from DeskMetrics Python Backend**  
> This document captures architectural decisions, patterns, and lessons learned from the Python implementation to guide C++ development.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Key Python Files to Port](#2-key-python-files-to-port)
3. [Instrument Manager](#3-instrument-manager)
4. [Margin System](#4-margin-system)
5. [Data Structures](#5-data-structures)
6. [Problems Faced & Solutions](#6-problems-faced--solutions)
7. [C++ Implementation Recommendations](#7-c-implementation-recommendations)
8. [Build & Integration](#8-build--integration)

---

## 1. Architecture Overview

### 1.1 Layered Architecture (Python → C++)

```
┌─────────────────────────────────────────────────────────┐
│                    API Layer                            │
│  Python: FastAPI routers (backend/app/api/)             │
│  C++:    REST handlers (Crow/Drogon/Pistache)           │
├─────────────────────────────────────────────────────────┤
│                  Domain Layer                           │
│  Python: backend/app/domain/ (business logic)           │
│  C++:    src/domain/ namespace (pure logic, no I/O)     │
├─────────────────────────────────────────────────────────┤
│               Infrastructure Layer                      │
│  Python: backend/app/infra/ (Kite API, DB, files)       │
│  C++:    src/infra/ namespace (HTTP client, storage)    │
├─────────────────────────────────────────────────────────┤
│                  Models Layer                           │
│  Python: Pydantic models, SQLAlchemy                    │
│  C++:    structs with JSON serialization                │
└─────────────────────────────────────────────────────────┘
```

### 1.2 C++ Namespace Mapping

```cpp
namespace deskmetrics {
    namespace domain {
        namespace instruments { }  // Instrument service
        namespace margin { }       // Margin calculations
        namespace payoff { }       // Payoff engine (NEW)
        namespace greeks { }       // Greeks calculations (NEW)
    }
    namespace infra {
        namespace kite { }         // Kite API client
        namespace cache { }        // Caching layer
        namespace config { }       // Configuration
    }
    namespace api {
        namespace handlers { }     // REST handlers
        namespace schemas { }      // Request/Response DTOs
    }
}
```

---

## 2. Key Python Files to Port

### 2.1 PRIORITY 1: Core Domain Logic (Copy These!)

| Python File | Purpose | C++ Target | Complexity |
|-------------|---------|------------|------------|
| `backend/app/domain/instruments/service.py` | **Instrument caching, symbol building, search** | `src/domain/instruments/service.hpp` | HIGH |
| `backend/app/api/margin.py` | **Margin calculation, fallback logic** | `src/domain/margin/calculator.hpp` | MEDIUM |
| `backend/app/infra/kite_client.py` | **Kite API wrapper** | `src/infra/kite/client.hpp` | MEDIUM |
| `backend/app/settings.py` | **Configuration management** | `src/infra/config/settings.hpp` | LOW |

### 2.2 PRIORITY 2: API Schemas (Data Structures)

| Python File | Purpose | C++ Target |
|-------------|---------|------------|
| `backend/app/api/instruments.py` | Instrument API schemas | `src/api/schemas/instrument.hpp` |
| `backend/app/api/margin.py` (schemas) | Margin request/response | `src/api/schemas/margin.hpp` |
| `backend/app/api/schemas.py` | Shared schemas | `src/api/schemas/common.hpp` |

### 2.3 PRIORITY 3: Reference/Inspiration

| Python File | Purpose | Why Useful |
|-------------|---------|------------|
| `inspiration/min_kite_login.py` | Kite auth flow | Token handling pattern |
| `inspiration/instrument_cache/*.csv` | Cached instruments | Test data, field reference |
| `inspiration/Kite_Documents/*.md` | Kite API docs | API contract reference |
| `inspiration/pykiteconnect-master/` | Kite SDK source | API implementation details |

### 2.4 Files to SKIP (Charges/Costs)

- `backend/app/domain/charges/` — Not needed
- `backend/app/domain/costs/` — Not needed
- `backend/app/api/ratecards.py` — Not needed

---

## 3. Instrument Manager

### 3.1 Overview

The Instrument Manager handles:
- Multi-tier caching (memory → disk → API)
- Symbol building from components
- Fuzzy search for instruments
- Bulk symbol resolution

### 3.2 Symbol Format Rules (CRITICAL)

```cpp
// From backend/app/domain/instruments/service.py lines 303-375

// === WEEKLY OPTIONS (Index options) ===
// Format: SYMBOL + YY + M + DD + STRIKE + CE/PE
// Month codes: 1-9 for Jan-Sep, O/N/D for Oct/Nov/Dec
// Examples:
//   NIFTY2510224000CE   → Jan 02, 2025, 24000 CE
//   BANKNIFTY25O0948000PE → Oct 09, 2025, 48000 PE
//   NIFTY25N1323500CE   → Nov 13, 2025, 23500 CE

// === MONTHLY OPTIONS ===
// Format: SYMBOL + YY + MMM + STRIKE + CE/PE
// Examples:
//   NIFTY26JAN26000CE   → Jan 2026 monthly, 26000 CE
//   RELIANCE25MAY1410CE → May 2025 monthly, 1410 CE

// === FUTURES ===
// Format: SYMBOL + YY + MMM + FUT
// Examples:
//   NIFTY25MAYFUT
//   BANKNIFTY26JANFUT
//   RELIANCE25JUNFUT
```

### 3.3 Weekly vs Monthly Detection

```cpp
// Symbols eligible for weekly expiry (from line 27-28 in service.py)
const std::unordered_set<std::string> WEEKLY_EXPIRY_SYMBOLS = {
    "NIFTY", "BANKNIFTY", "FINNIFTY", "MIDCPNIFTY", "SENSEX", "BANKEX"
};

// If underlying is in this set AND it's an option → weekly format
// Otherwise → monthly format
```

### 3.4 Month Code Logic

```cpp
// From _get_month_code() in service.py lines 385-395
char get_month_code(int month) {
    if (month >= 1 && month <= 9) return '0' + month;  // '1' to '9'
    if (month == 10) return 'O';
    if (month == 11) return 'N';
    if (month == 12) return 'D';
    throw std::invalid_argument("Invalid month");
}
```

### 3.5 Caching Strategy

```
┌─────────────────┐
│   API Request   │
└────────┬────────┘
         ▼
┌─────────────────┐    HIT
│  Memory Cache   │────────► Return DataFrame
│  (24hr expiry)  │
└────────┬────────┘
         │ MISS
         ▼
┌─────────────────┐    HIT
│   Disk Cache    │────────► Load CSV, Update Memory
│  (24hr expiry)  │
└────────┬────────┘
         │ MISS
         ▼
┌─────────────────┐
│   Kite API      │────────► Save to Disk, Update Memory
│ /instruments    │
└─────────────────┘
```

### 3.6 C++ Implementation Sketch

```cpp
// src/domain/instruments/instrument.hpp
#pragma once
#include <string>
#include <optional>
#include <cstdint>

namespace deskmetrics::domain::instruments {

struct Instrument {
    uint32_t instrument_token;
    uint32_t exchange_token;
    std::string tradingsymbol;
    std::string name;
    std::string exchange;           // NSE, NFO, BSE, BFO, CDS, MCX
    std::string segment;            // NFO-FUT, NFO-OPT, etc.
    std::string instrument_type;    // FUT, CE, PE, EQ
    uint16_t lot_size;
    double tick_size;
    std::optional<std::string> expiry;  // YYYY-MM-DD
    std::optional<double> strike;
    
    // JSON serialization (use nlohmann/json)
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Instrument,
        instrument_token, exchange_token, tradingsymbol, name,
        exchange, segment, instrument_type, lot_size, tick_size,
        expiry, strike)
};

enum class ExchangeType { NSE, NFO, BSE, BFO, CDS, MCX };

// Convert string to enum
ExchangeType parse_exchange(const std::string& s);

} // namespace
```

---

## 4. Margin System

### 4.1 Dual-Mode Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  Margin Calculator                      │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌─────────────┐          ┌──────────────────┐         │
│  │  Kite API   │   OR     │  Fallback Mode   │         │
│  │  (Primary)  │          │  (Estimation)    │         │
│  └──────┬──────┘          └────────┬─────────┘         │
│         │                          │                    │
│         ▼                          ▼                    │
│  basket_margins()          Rule-based calc              │
│  - SPAN margin             - Symbol pattern parsing     │
│  - Exposure margin         - Hardcoded per-lot values   │
│  - Premium                 - 60% spread benefit         │
│  - Spread benefit          - Rough estimates only       │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### 4.2 Fallback Margin Formulas

From `margin.py` lines 152-220:

```cpp
// Lot sizes (can change - these are current as of 2025)
constexpr int LOT_SIZE_BANKNIFTY = 35;
constexpr int LOT_SIZE_NIFTY = 75;
constexpr int LOT_SIZE_DEFAULT = 50;

// Per-lot margin estimates (rough, for fallback only)
struct MarginEstimates {
    // Options SELL (per lot)
    static constexpr double OPTION_SELL_BANKNIFTY = 100000.0;  // ~1 lakh
    static constexpr double OPTION_SELL_NIFTY = 80000.0;       // ~80k
    
    // Options BUY (premium estimate per lot)
    static constexpr double OPTION_BUY_PREMIUM = 5000.0;       // ~5k
    
    // Futures (per lot)
    static constexpr double FUTURE_BANKNIFTY = 240000.0;       // ~2.4 lakh
    static constexpr double FUTURE_NIFTY = 170000.0;           // ~1.7 lakh
    static constexpr double FUTURE_DEFAULT = 150000.0;         // ~1.5 lakh
    
    // Spread benefit multiplier (60% reduction → multiply by 0.4)
    static constexpr double SPREAD_BENEFIT = 0.4;
};
```

### 4.3 Symbol Pattern Detection

```cpp
// From margin.py lines 166-175
bool is_option(const std::string& symbol) {
    return symbol.ends_with("CE") || symbol.ends_with("PE");
}

bool is_future(const std::string& symbol) {
    return symbol.find("FUT") != std::string::npos;
}

bool is_banknifty(const std::string& symbol) {
    return symbol.find("BANKNIFTY") != std::string::npos;
}

bool is_nifty(const std::string& symbol) {
    return symbol.find("NIFTY") != std::string::npos && !is_banknifty(symbol);
}
```

### 4.4 Margin Breakdown Structure

```cpp
// From margin.py lines 41-48
struct MarginBreakdown {
    double total;
    double span;              // SPAN margin
    double exposure;          // Exposure margin
    double option_premium;    // Premium for option buys
    std::optional<double> additional;
    std::optional<double> var;  // Value at Risk
};

struct BasketMarginResponse {
    MarginBreakdown initial;  // Before spread benefit
    MarginBreakdown final;    // After spread benefit
    std::string source;       // "kite" or "estimated"
    std::optional<std::vector<PerLegMargin>> per_leg;
};
```

---

## 5. Data Structures

### 5.1 Strategy Leg (for Payoff Engine)

```cpp
// From margin.py lines 107-114
struct StrategyLeg {
    std::string id;           // UUID
    Instrument instrument;
    std::string side;         // "BUY" or "SELL"
    int quantity;
    std::string product;      // "MIS", "NRML", "CNC"
    std::optional<double> entry_price;
    bool locked = false;
};
```

### 5.2 Margin Order

```cpp
// From margin.py lines 20-32
struct MarginOrder {
    std::string exchange;         // "NFO", "NSE", etc.
    std::string tradingsymbol;    // "NIFTY25JANFUT"
    std::string transaction_type; // "BUY" or "SELL"
    int quantity;
    std::string product;          // "MIS", "NRML", "CNC"
    std::string order_type = "MARKET";
    std::optional<double> price;
    std::optional<double> trigger_price;
    std::optional<std::string> variety;
};
```

### 5.3 Configuration Settings

```cpp
// From settings.py lines 9-70
struct Settings {
    // Storage
    std::string file_store_path = "./.data/files";
    std::string database_url = "sqlite:///./.data/deskmetrics.sqlite3";
    
    // Kite credentials
    std::optional<std::string> kite_api_key;
    std::optional<std::string> kite_access_token;
    std::optional<std::string> kite_token_url;
    
    // Annualization constants
    int annualization_discrete_days = 240;   // Trading days/year
    int annualization_continuous_days = 365;
    
    // Interest rates (for margin interest calculations)
    double interest_rate_intraday = 0.06;    // 6%
    double interest_rate_overnight = 0.12;   // 12%
    double risk_free_rate = 0.065;           // 6.5%
    
    // Currency
    std::string currency_code = "INR";
    int currency_decimals = 2;
};
```

---

## 6. Problems Faced & Solutions

### 6.1 Symbol Parsing is Fragile

**Problem**: Different expiry formats for weekly vs monthly options.

```
NIFTY2510224000CE  → Weekly (Jan 02, 2025)
NIFTY26JAN26000CE  → Monthly (Jan 2026)
```

**Solution in Python** (lines 410-475 in service.py):
- Try BOTH formats when symbol not in `WEEKLY_EXPIRY_SYMBOLS`
- For index options, try weekly first (more common for active trading)
- Fall back to suggestions if neither matches

**C++ Recommendation**:
```cpp
std::vector<std::string> build_symbol_candidates(
    const std::string& underlying,
    const Date& expiry,
    InstrumentType type,
    std::optional<double> strike
) {
    std::vector<std::string> candidates;
    
    if (type == InstrumentType::CE || type == InstrumentType::PE) {
        if (WEEKLY_EXPIRY_SYMBOLS.contains(underlying)) {
            candidates.push_back(build_weekly_symbol(...));
        }
        candidates.push_back(build_monthly_symbol(...));
    } else {
        candidates.push_back(build_futures_symbol(...));
    }
    
    return candidates;
}
```

### 6.2 Lot Size Changes Over Time

**Problem**: BANKNIFTY lot size changed from 30 → 35.

**Solution**: 
- Always fetch lot size from instrument data
- Don't hardcode lot sizes for calculations
- Only use hardcoded values in fallback estimation

### 6.3 Kite API Unavailability

**Problem**: Need to work offline or when API is down.

**Solution in Python** (margin.py lines 152-220):
- Detect availability via `kite_provider.is_available`
- Fall back to rule-based estimation
- Mark response with `source: "estimated"` so UI can warn user

### 6.4 Date Format Variations

**Problem**: Multiple date formats in inputs.

```python
# From service.py lines 323-333
formats = ["%Y-%m-%d", "%d-%m-%Y", "%d/%m/%Y", "%Y/%m/%d", "%d-%b-%Y", "%d %b %Y"]
```

**C++ Recommendation**: Use Howard Hinnant's date library:
```cpp
#include <date/date.h>

std::optional<date::year_month_day> parse_date(const std::string& s) {
    std::istringstream in{s};
    date::year_month_day ymd;
    
    // Try each format
    for (const char* fmt : {"%Y-%m-%d", "%d-%m-%Y", "%d/%m/%Y"}) {
        in.clear();
        in.str(s);
        if (in >> date::parse(fmt, ymd)) return ymd;
    }
    return std::nullopt;
}
```

### 6.5 Memory Cache Invalidation

**Problem**: Stale instrument data after market hours.

**Solution**:
- 24-hour expiry on both memory and disk cache
- Force refresh endpoint (`/instruments/refresh`)
- Fallback to stale cache if API fails (better than nothing)

### 6.6 Fuzzy Search Performance

**Problem**: Searching 30,000+ instruments is slow.

**Solution in Python** (service.py lines 210-300):
- Pre-compute `search_text` column: `tradingsymbol + name + exchange`
- Use pandas vectorized string operations
- Filter progressively (exchange → segment → type → text)

**C++ Recommendation**:
```cpp
// Build search index at load time
std::unordered_map<std::string, std::vector<size_t>> search_index;

for (size_t i = 0; i < instruments.size(); ++i) {
    auto tokens = tokenize_lower(instruments[i].tradingsymbol);
    for (const auto& token : tokens) {
        search_index[token].push_back(i);
    }
}
```

---

## 7. C++ Implementation Recommendations

### 7.1 Recommended Libraries

| Purpose | Library | Why |
|---------|---------|-----|
| JSON | `nlohmann/json` | Header-only, great API |
| HTTP Client | `cpr` or `libcurl` | For Kite API calls |
| HTTP Server | `Crow` or `Drogon` | REST API |
| CSV Parsing | `fast-cpp-csv-parser` | Fast, header-only |
| Date/Time | `date` (Howard Hinnant) | Correct date handling |
| Logging | `spdlog` | Fast, header-only |
| Config | `toml++` or `yaml-cpp` | Settings files |

### 7.2 Project Structure

```
cpp-options-platform/
├── CMakeLists.txt
├── vcpkg.json              # Dependencies
├── include/
│   └── deskmetrics/
│       ├── domain/
│       │   ├── instruments/
│       │   │   ├── instrument.hpp
│       │   │   ├── service.hpp
│       │   │   └── symbol_builder.hpp
│       │   ├── margin/
│       │   │   ├── calculator.hpp
│       │   │   └── models.hpp
│       │   ├── payoff/
│       │   │   ├── engine.hpp
│       │   │   └── strategy.hpp
│       │   └── greeks/
│       │       ├── black_scholes.hpp
│       │       └── calculator.hpp
│       ├── infra/
│       │   ├── kite/
│       │   │   └── client.hpp
│       │   ├── cache/
│       │   │   └── instrument_cache.hpp
│       │   └── config/
│       │       └── settings.hpp
│       └── api/
│           └── schemas/
├── src/
│   ├── domain/
│   ├── infra/
│   └── api/
├── tests/
├── data/
│   └── instruments/        # Cached CSVs
└── config/
    └── settings.toml
```

### 7.3 Build System (CMake)

```cmake
cmake_minimum_required(VERSION 3.20)
project(deskmetrics_cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies (via vcpkg)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Crow CONFIG REQUIRED)

add_library(deskmetrics_core
    src/domain/instruments/service.cpp
    src/domain/margin/calculator.cpp
    src/infra/kite/client.cpp
    src/infra/cache/instrument_cache.cpp
)

target_link_libraries(deskmetrics_core
    PRIVATE
        nlohmann_json::nlohmann_json
        cpr::cpr
        spdlog::spdlog
)
```

### 7.4 Performance Targets

From the spec (Section 9):
- **Payoff recompute**: <100ms
- **Greeks calculation**: <10ms per leg
- **Instrument search**: <50ms

**C++ advantages**:
- SIMD for Greeks calculations
- Zero-copy string views for search
- Memory-mapped CSV files for instrument cache

---

## 8. Build & Integration

### 8.1 Python Interop (Optional)

If gradual migration is needed:

```cpp
// pybind11 wrapper
#include <pybind11/pybind11.h>

PYBIND11_MODULE(deskmetrics_cpp, m) {
    py::class_<Instrument>(m, "Instrument")
        .def_readonly("instrument_token", &Instrument::instrument_token)
        .def_readonly("tradingsymbol", &Instrument::tradingsymbol)
        // ...
    ;
    
    py::class_<InstrumentService>(m, "InstrumentService")
        .def("search", &InstrumentService::search)
        .def("build_tradingsymbol", &InstrumentService::build_tradingsymbol)
    ;
}
```

### 8.2 Testing Strategy

```cpp
// Translate Python tests
// From backend/tests/test_*.py

TEST_CASE("Symbol building - weekly NIFTY option") {
    auto service = InstrumentService();
    
    auto symbol = service.build_tradingsymbol(
        "NIFTY",                    // underlying
        Date{2025, 1, 2},           // expiry
        InstrumentType::CE,         // type
        24000                       // strike
    );
    
    REQUIRE(symbol == "NIFTY2510224000CE");
}

TEST_CASE("Margin fallback - spread benefit") {
    auto calc = MarginCalculator();
    
    std::vector<MarginOrder> orders = {
        {"NFO", "NIFTY25JANFUT", "BUY", 75, "NRML"},
        {"NFO", "NIFTY25JANFUT", "SELL", 75, "NRML"},
    };
    
    auto result = calc.calculate_fallback(orders);
    
    // Spread should get 60% benefit
    REQUIRE(result.source == "estimated");
    REQUIRE(result.final.total < result.initial.total);
}
```

---

## Quick Reference: File Mapping

| What You Need | Python Source | Lines | C++ Target |
|--------------|---------------|-------|------------|
| Instrument struct | `domain/instruments/service.py` | 43-62 | `instrument.hpp` |
| Symbol building | `domain/instruments/service.py` | 303-395 | `symbol_builder.hpp` |
| Month codes | `domain/instruments/service.py` | 385-395 | `symbol_builder.hpp` |
| Fuzzy search | `domain/instruments/service.py` | 203-300 | `service.hpp` |
| Cache strategy | `domain/instruments/service.py` | 87-200 | `instrument_cache.hpp` |
| Margin fallback | `api/margin.py` | 152-220 | `calculator.hpp` |
| Margin schemas | `api/margin.py` | 20-115 | `models.hpp` |
| Kite client | `infra/kite_client.py` | 1-100 | `kite/client.hpp` |
| Settings | `settings.py` | 9-83 | `settings.hpp` |

---

## Next Steps

1. **Start with instrument data structures** — Get `Instrument` struct and JSON serialization working
2. **Implement symbol builder** — Most complex piece, translate from Python line-by-line
3. **Add CSV cache loader** — Load instruments from `inspiration/instrument_cache/*.csv`
4. **Build margin calculator** — Start with fallback mode (no Kite dependency)
5. **Add Kite client** — HTTP wrapper for live margin calls
6. **Build payoff engine** — This is the NEW functionality not in Python yet

---

*Document generated from DeskMetrics Python codebase analysis*  
*Last updated: 2026-01-04*
