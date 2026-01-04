# Python Files Reference for C++ Translation

> Quick reference list of which Python files to copy and translate for the C++ Options Platform.

---

## 🎯 MUST COPY (Core Logic)

These files contain the core business logic that needs to be carefully translated:

### 1. Instrument Service (`backend/app/domain/instruments/service.py`)
**718 lines | HIGHEST PRIORITY**

```
What's inside:
├── Lines 43-62:   Instrument dataclass (→ struct Instrument)
├── Lines 65-76:   InstrumentCache dataclass (→ class InstrumentCache)
├── Lines 27-28:   WEEKLY_EXPIRY_SYMBOLS set (→ const unordered_set)
├── Lines 85-200:  Cache loading/saving (disk + memory)
├── Lines 203-300: Fuzzy search algorithm
├── Lines 303-395: Symbol building (weekly vs monthly formats)
├── Lines 385-395: Month code encoding (1-9, O, N, D)
├── Lines 398-540: build_and_validate_symbol() with fallback
├── Lines 543-605: resolve_symbol() single lookup
├── Lines 608-715: resolve_symbols_bulk() batch resolution
```

**Key functions to translate:**
- `build_tradingsymbol()`
- `_get_month_code()`
- `search()`
- `resolve_symbol()`
- `get_instruments()` / `get_all_instruments()`

---

### 2. Margin API (`backend/app/api/margin.py`)
**763 lines | HIGH PRIORITY**

```
What's inside:
├── Lines 20-32:   MarginOrder schema
├── Lines 35-48:   MarginBreakdown schema
├── Lines 50-78:   Response schemas (ChargeItem, PerLegMargin, etc.)
├── Lines 107-120: StrategyLeg schema (for payoff builder)
├── Lines 152-220: calculate_fallback_margin() ← CRITICAL
├── Lines 223-310: calculate_basket_margin() main endpoint
├── Lines 313-450: CSV upload handling
├── Lines 453-600: Column mapping and resolution
├── Lines 603-763: Strategy building from CSV
```

**Key functions to translate:**
- `calculate_fallback_margin()` — Must have for offline mode
- Margin estimation formulas (lines 170-205)
- Spread benefit calculation (lines 207-214)

---

### 3. Kite Client (`backend/app/infra/kite_client.py`)
**376 lines | MEDIUM PRIORITY**

```
What's inside:
├── Lines 1-35:    Imports and error classes
├── Lines 38-70:   _fetch_access_token_from_url()
├── Lines 73-180:  KiteClient class init and auth
├── Lines 183-250: basket_margins() wrapper
├── Lines 253-320: instruments() wrapper
├── Lines 323-376: Helper methods
```

**Key parts to translate:**
- Token fetching from URL
- Error handling (CredentialError, KiteClientError)
- API request patterns

---

### 4. Settings (`backend/app/settings.py`)
**83 lines | LOW PRIORITY but needed**

```
What's inside:
├── Lines 9-70:    Settings class with all configs
├── Lines 30-44:   Annualization constants
├── Lines 46-58:   Interest rates
├── Lines 73-83:   Singleton getter
```

**C++ equivalent:** Use TOML or YAML config file + loader class

---

### 5. Instruments API (`backend/app/api/instruments.py`)
**156 lines | LOW PRIORITY (thin wrapper)**

```
What's inside:
├── Lines 15-32:   InstrumentResult schema
├── Lines 35-55:   SearchResponse, ResolveRequest, etc.
├── Lines 58-93:   /search endpoint
├── Lines 96-115:  /resolve endpoint
├── Lines 118-145: /refresh endpoint
```

---

## 📁 REFERENCE FILES (Don't translate, use for understanding)

### Kite Documentation
```
inspiration/Kite_Documents/
├── kite_margin.md          ← Margin API contract
├── kite_historical_data.md ← Historical data API
├── kite_ltp_doc.md         ← Live price API
└── kite_websocket_doc.md   ← WebSocket streaming
```

### Instrument Cache (TEST DATA)
```
inspiration/instrument_cache/
├── nfo_instruments.csv     ← ~32,000 F&O instruments
├── nse_instruments.csv     ← NSE equities
├── bse_instruments.csv     ← BSE equities
├── bfo_instruments.csv     ← BSE F&O
└── cds_instruments.csv     ← Currency derivatives
```

**Use these CSVs for:**
- Understanding instrument data schema
- Unit testing without API
- Field names and types

### Kite Python SDK (Reference implementation)
```
inspiration/pykiteconnect-master/
├── kiteconnect/
│   ├── connect.py          ← Main client class
│   ├── exceptions.py       ← Error types
│   └── ticker.py           ← WebSocket client
└── tests/                  ← Test patterns
```

---

## 🔧 SCHEMAS TO EXTRACT

From `backend/app/api/margin.py`:

```cpp
// Copy these verbatim and translate to C++ structs

struct MarginOrder {
    std::string exchange;          // "NFO", "NSE", etc.
    std::string tradingsymbol;     // "NIFTY25JANFUT"
    std::string transaction_type;  // "BUY" or "SELL"
    int quantity;
    std::string product;           // "MIS", "NRML", "CNC"
    std::string order_type = "MARKET";
    std::optional<double> price;
    std::optional<double> trigger_price;
};

struct MarginBreakdown {
    double total;
    double span;
    double exposure;
    double option_premium;
    std::optional<double> additional;
    std::optional<double> var;
};

struct StrategyLeg {
    std::string id;
    Instrument instrument;
    std::string side;           // "BUY" or "SELL"
    int quantity;
    std::string product;
    std::optional<double> entry_price;
    bool locked = false;
};
```

---

## 📋 Translation Checklist

### Phase 1: Data Structures
- [ ] `Instrument` struct from `service.py:43-62`
- [ ] `MarginOrder` from `margin.py:20-32`
- [ ] `MarginBreakdown` from `margin.py:35-48`
- [ ] `StrategyLeg` from `margin.py:107-114`
- [ ] `Settings` from `settings.py:9-70`

### Phase 2: Instrument Manager
- [ ] `InstrumentCache` class from `service.py:65-76`
- [ ] CSV loading from disk
- [ ] Memory cache with expiry
- [ ] `build_tradingsymbol()` from `service.py:303-395`
- [ ] `_get_month_code()` from `service.py:385-395`
- [ ] `search()` from `service.py:203-300`

### Phase 3: Margin Calculator
- [ ] `calculate_fallback_margin()` from `margin.py:152-220`
- [ ] Symbol pattern detection (is_option, is_future, etc.)
- [ ] Lot size lookup
- [ ] Spread benefit calculation

### Phase 4: Kite Client
- [ ] HTTP client wrapper
- [ ] Token fetching from URL
- [ ] `basket_margins()` API call
- [ ] `instruments()` API call
- [ ] Error handling

### Phase 5: NEW - Payoff Engine
- [ ] Black-Scholes pricing
- [ ] Greeks calculation (Delta, Gamma, Theta, Vega, Rho)
- [ ] Payoff curve generation
- [ ] Scenario analysis

---

## 🚀 Quick Start Commands

```bash
# Copy essential files to a reference folder
mkdir -p cpp_reference/python_source

# Core files
cp backend/app/domain/instruments/service.py cpp_reference/python_source/
cp backend/app/api/margin.py cpp_reference/python_source/
cp backend/app/infra/kite_client.py cpp_reference/python_source/
cp backend/app/settings.py cpp_reference/python_source/

# Test data
cp -r inspiration/instrument_cache cpp_reference/test_data/

# Documentation
cp -r inspiration/Kite_Documents cpp_reference/docs/
```

---

## Line Count Summary

| File | Lines | Priority | Notes |
|------|-------|----------|-------|
| `domain/instruments/service.py` | 718 | 🔴 CRITICAL | Symbol building, search |
| `api/margin.py` | 763 | 🔴 CRITICAL | Margin calc, schemas |
| `infra/kite_client.py` | 376 | 🟡 HIGH | API wrapper |
| `settings.py` | 83 | 🟢 LOW | Config |
| `api/instruments.py` | 156 | 🟢 LOW | Thin wrapper |
| **TOTAL** | ~2,100 | | Core logic to translate |

---

*This document is a companion to `cpp_port_guide.md`*
