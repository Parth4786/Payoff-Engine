# C++ Project Bootstrap Prompt

Copy this entire document and paste it into an LLM when starting a new session.

It contains everything needed to build the Options Payoff + Market Observatory platform in C++ from scratch.

---

## 🎯 PROJECT OVERVIEW

You are building a **production-grade Options Strategy Builder & Payoff Analytics Platform** in C++20.

This is NOT a toy. It must be:
- Deterministic (same inputs → same outputs)
- Auditable (every result reproducible)
- Simulation-safe (no future data leakage)
- Defensive (handle bad/partial/stale data gracefully)

**Two product lines share the same core:**
1. **Execution Observatory** — L2 microstructure analytics (spread, imbalance, depth, execution hints)
2. **Payoff Platform** — options strategy builder, Greeks, scenarios, time-decay payoff

---

## 📁 REFERENCE MATERIAL (from Python repo)

The Python implementation is the behavioral spec. Key files to reference:

### Core contracts (translate these first)
```
core/models.py          → DepthLevel, TradeInfo, DepthSnapshot + validation
core/datasource.py      → MarketDataSource interface (replay + stream)
core/instrument_manager.py → CSV parsing, token mapping, canonical symbol
```

### Data sources (real schemas)
```
data_sources/clickhouse_source.py → ClickHouse row shape, replay/stream patterns
data_sources/kite_ws_source.py    → Kite tick normalization, identity mapping
data_sources/failover_source.py   → Per-symbol failover logic
```

### Streaming safety (defensive pipeline)
```
streaming/assembler.py    → Merge partial updates into full book
streaming/dedup.py        → Duplicate + out-of-order handling
streaming/gap_detector.py → Staleness detection
```

### Feature engine (pure math)
```
features/engine.py → FeatureSnapshot, compute_features()
```

### Execution hints
```
execution/hint_engine.py → PASSIVE/AGGRESSIVE/WAIT + reasons
execution/models.py      → ExecutionHint, Posture enum
```

### Docs (read these for rules)
```
docs/DATA_CONTRACT.md        → Canonical model invariants
docs/STREAMING_ENGINE.md     → Defensive streaming rules
docs/KITE_FALLBACK.md        → Identity mapping (instrument_token vs exchange_token)
docs/payoff/payoff_feature_spec.md → Payoff platform requirements
docs/cpp/API_DTO_SCHEMA.md   → Wire-level JSON shapes
docs/cpp/DATA_SHAPES.md      → What ClickHouse/Kite data looks like
```

---

## 🚫 NON-NEGOTIABLES (bugs if violated)

1. **Replay and streaming use identical downstream logic**
2. **Frontend never computes features** (render only)
3. **All feeds normalize into one internal model** (`DepthSnapshot`)
4. **No future leakage** — simulation sees only past/current data
5. **Safe degradation** — `WAIT` is always valid; stale/partial flags are explicit
6. **Identity mapping** — `instrument_id = exchange_token` (never guess)

---

## ✅ TASK CHECKLIST (implement in order)

### Phase 1: Core Foundation
- [ ] **Task 1.1**: Create CMake project structure
  ```
  cpp/
  ├── CMakeLists.txt
  ├── src/
  │   ├── core/
  │   ├── datasource/
  │   ├── features/
  │   ├── execution/
  │   ├── payoff/
  │   └── api/
  ├── include/
  └── tests/
  ```

- [ ] **Task 1.2**: Implement `core/models.hpp`
  - `DepthLevel` struct (price, size, orders)
  - `TradeInfo` struct (last_price, volumes, OI, timestamps, OHLC)
  - `DepthSnapshot` struct (instrument_id, symbol, exchange_timestamp, bids, asks, trade, source, is_partial, is_stale)
  - Validation function (enforce invariants)
  - Reference: `core/models.py`

- [ ] **Task 1.3**: Implement `core/instrument_manager.hpp`
  - Load CSV files (instrument_token, exchange_token, tradingsymbol, lot_size, tick_size, etc.)
  - Build lookup maps:
    - `instrument_token → InstrumentInfo`
    - `canonical_symbol (exchange:exchange_token) → InstrumentInfo`
    - `tradingsymbol → vector<InstrumentInfo>`
  - Reference: `core/instrument_manager.py`

- [ ] **Task 1.4**: Implement `core/datasource.hpp`
  - Abstract interface `MarketDataSource`
  - Methods: `get_symbols()`, `replay_snapshots()`, `stream_snapshots()`
  - Reference: `core/datasource.py`

### Phase 2: Data Sources
- [ ] **Task 2.1**: Implement `datasource/clickhouse_source.cpp`
  - Connect via HTTP or native client
  - Replay: bounded time range query, ordered by exchange_timestamp
  - Stream: watermark polling pattern
  - Row → `DepthSnapshot` normalization
  - Reference: `data_sources/clickhouse_source.py`

- [ ] **Task 2.2**: Implement `datasource/mock_source.cpp`
  - Deterministic test data
  - Reference: `data_sources/mock_source.py`

### Phase 3: Streaming Pipeline
- [ ] **Task 3.1**: Implement `streaming/assembler.cpp`
  - Per-symbol book state
  - Merge partial updates (only bids OR only asks)
  - Reference: `streaming/assembler.py`

- [ ] **Task 3.2**: Implement `streaming/dedup.cpp`
  - Drop duplicates by (symbol, exchange_timestamp)
  - Drop out-of-order (exchange_timestamp < last_seen)
  - Count drops for observability
  - Reference: `streaming/dedup.py`

- [ ] **Task 3.3**: Implement `streaming/gap_detector.cpp`
  - Mark `is_stale=true` when gap > threshold
  - Configurable thresholds (100-500ms)
  - Reference: `streaming/gap_detector.py`

### Phase 4: Features Engine
- [ ] **Task 4.1**: Implement `features/engine.cpp`
  - Pure function: `compute_features(snapshot, prev_snapshot, tick_size) → FeatureSnapshot`
  - Metrics: midprice, spread, imbalance, microprice, depth_slope, LPI, OFI, shock
  - Reference: `features/engine.py`

### Phase 5: Execution Hints
- [ ] **Task 5.1**: Implement `execution/hint_engine.cpp`
  - Postures: `PASSIVE`, `AGGRESSIVE`, `WAIT`
  - Rules based on spread, depth_slope, shock, staleness
  - Explainable reasons
  - Reference: `execution/hint_engine.py`

### Phase 6: Payoff Engine (new)
- [ ] **Task 6.1**: Implement `payoff/models.hpp`
  - `OptionLeg` (type, side, strike, expiry, quantity, premium)
  - `Strategy` (vector of legs)
  - `Scenario` (spot_shift, iv_shift, time_shift)

- [ ] **Task 6.2**: Implement `payoff/pricing.cpp`
  - Black-Scholes pricing (call/put)
  - Greeks: delta, gamma, theta, vega, rho
  - Reference: standard BS formulas

- [ ] **Task 6.3**: Implement `payoff/calculator.cpp`
  - Expiry payoff curve
  - Time-dependent payoff (today, T+1, custom)
  - Scenario payoff (multiple curves overlaid)
  - Breakevens, max profit, max loss

- [ ] **Task 6.4**: Implement `payoff/risk.cpp`
  - Probability of profit (model-driven)
  - Tail loss (5%, 1%)

### Phase 7: API Layer
- [ ] **Task 7.1**: Implement REST API
  - `GET /symbols`
  - `GET /replay/snapshots`
  - `GET /replay/features`
  - `POST /strategy/build`
  - `POST /payoff/calculate`
  - `POST /greeks/calculate`
  - `POST /scenario/run`

- [ ] **Task 7.2**: Implement WebSocket streaming
  - `WS /stream/snapshots`
  - `WS /stream/features`

### Phase 8: Testing
- [ ] **Task 8.1**: Unit tests for models (invariant enforcement)
- [ ] **Task 8.2**: Unit tests for features (determinism)
- [ ] **Task 8.3**: Parity tests (replay vs stream produce same output)
- [ ] **Task 8.4**: Payoff tests (match analytical formulas)

---

## 🔧 TECH STACK (recommended)

```
Build:        CMake 3.20+
Package mgr:  vcpkg or Conan
HTTP server:  Boost.Beast or cpp-httplib
JSON:         nlohmann/json
Logging:      spdlog
Testing:      GoogleTest or Catch2
ClickHouse:   clickhouse-cpp or HTTP API
Time:         std::chrono (millisecond precision for exchange_timestamp)
```

---

## 📝 FILE TRANSLATION PROMPT

When translating a specific Python file, use this prompt:

```
Translate the following Python module into production-grade C++20.

Requirements:
- Preserve behavior exactly
- No future leakage (replay/stream logic uses only past/current data)
- Enforce data-contract invariants (bids DESC, asks ASC, best_bid < best_ask)
- No global mutable state
- Explicit error handling (std::optional, std::expected, or exceptions)
- Deterministic outputs
- Use std::chrono for time, std::optional for nullable fields

Output:
- Header (.hpp) and implementation (.cpp)
- Brief unit tests for invariants
- Note any C++-specific choices

Python code:
<PASTE FILE CONTENT>
```

---

## 🎬 HOW TO START

1. **Create the project structure** (Task 1.1)
2. **Implement models first** — this is the contract everything depends on
3. **Add mock data source** — lets you test without ClickHouse
4. **Build features engine** — pure function, easy to test
5. **Add streaming pipeline** — assembler → dedup → gap detector
6. **Add ClickHouse source** — real data
7. **Add payoff engine** — option pricing + scenarios
8. **Wire up API** — REST + WebSocket
9. **Write parity tests** — replay vs stream must match

---

## 💡 TIPS

- **Start with tests**: Write the test for what you want, then implement
- **One file at a time**: Don't try to port everything at once
- **Keep models immutable**: Use `const` and value types
- **Log everything**: Source switches, drops, staleness — these save debugging time
- **Don't guess mappings**: If instrument_token → exchange_token lookup fails, log and drop

---

## 🔗 INSTRUMENT MANAGER NOTES

The Python repo's `core/instrument_manager.py` handles:
- Loading CSVs from `docs/kite_inspiration/instrument master/`
- Building fast lookups by token, symbol, canonical_symbol
- Canonical symbol format: `{exchange}:{exchange_token}` (e.g., `NFO:49543`)

If you have a better margin/instrument manager from another repo, integrate it but ensure:
- It provides `instrument_token → exchange_token` mapping
- It provides `lot_size` and `tick_size` per instrument
- It uses the same canonical symbol format

---

## 📊 DATA SHAPES QUICK REFERENCE

### ClickHouse row (wide snapshot)
```
instrument_id, exchange, exchange_timestamp,
bid_price_0..4, bid_size_0..4, bid_orders_0..4,
ask_price_0..4, ask_size_0..4, ask_orders_0..4,
last_price, last_traded_quantity, total_traded_quantity, ...
```

### Kite tick (JSON-like)
```json
{
  "instrument_token": 408065,
  "last_price": 1412.95,
  "depth": {
    "buy": [{"price": 1412.90, "quantity": 10, "orders": 2}, ...],
    "sell": [{"price": 1412.95, "quantity": 5191, "orders": 13}, ...]
  }
}
```

### Canonical internal model
```cpp
struct DepthSnapshot {
  uint32_t instrument_id;  // = exchange_token
  std::string symbol;      // = "NFO:49543"
  Timestamp exchange_timestamp;
  std::vector<DepthLevel> bids;  // sorted DESC
  std::vector<DepthLevel> asks;  // sorted ASC
  TradeInfo trade;
  Source source;  // clickhouse | kite_ws | mock
  bool is_partial;
  bool is_stale;
};
```

---

## 🚀 READY TO START

Paste this entire document into your LLM and say:

> "Let's start with Task 1.1: Create the CMake project structure for a C++20 project using vcpkg for dependencies."

Then proceed task by task. Good luck!
