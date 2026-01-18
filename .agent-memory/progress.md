# Payoff Engine - C++ Backend Implementation Progress

## Current Status: ✅ BUILD SUCCESSFUL | TESTS PASSING | BACKEND FULLY READY FOR FRONTEND

### LATEST UPDATE - January 18, 2026 (Session 2)

**Session Goal:** Implement Replay API endpoints for historical backtesting

## COMPLETED THIS SESSION ✅

### 5. Replay Service API (Historical Strategy Backtesting)
- **NEW FILES CREATED**:
  - `include/api/replay_service.hpp` - Service interface with data structures
  - `src/api/replay_service.cpp` - Full implementation with ClickHouse integration
  - `src/api/replay_routes.cpp` - REST API route handlers

- **Replay Service Data Structures** (in replay_service.hpp):
  - `StrategySnapshot`: Timestamp, underlying price, PnL, Greeks, leg prices
  - `StrategyReplayRequest/Result`: Replay strategy over time range
  - `PredictionRequest/Result`: Get predicted payoff at historical time T
  - `CompareRequest/Result`: Compare prediction vs actual outcome
  - `ReplaySession`: Session management for step-through replay
  - `MarketEvent`: Event markers (IV spike, gap, volume surge)
  - `BatchPayoffRequest/Result`: Batch historical payoff calculation

- **Replay API Endpoints** (in replay_routes.cpp):
  | Endpoint | Method | Description |
  |----------|--------|-------------|
  | `/api/replay/strategy` | POST | Run strategy through historical data |
  | `/api/replay/prediction` | POST | Get predicted payoff at time T |
  | `/api/replay/compare` | POST | Compare prediction vs reality |
  | `/api/replay/session/create` | POST | Create replay session |
  | `/api/replay/session/state` | GET | Get current session state |
  | `/api/replay/session/step` | POST | Step session forward |
  | `/api/replay/session/seek` | POST | Seek to timestamp |
  | `/api/replay/session` | DELETE | Delete session |
  | `/api/replay/events` | GET | Get market events in range |
  | `/api/payoff/historical-batch` | POST | Batch historical payoff |

- **Files Changed**:
  - `CMakeLists.txt` - Added replay_service.cpp, replay_routes.cpp to payoff_api
  - `src/api/rest_server.cpp` - Added `setup_replay_routes()` call

- **Build Status**: ✅ 33/33 tests passing

---

## PREVIOUS SESSION (Session 1) ✅

### 0. Fixed Runtime DLL Issue (Static Linking)
- Executable was silently crashing with error `0xC0000139` (Entry Point Not Found)
- Root cause: Dynamic linking required MinGW runtime DLLs not in PATH
- **Solution**: Rebuilt with static linking flags in CMake
  ```cmake
  cmake -DCMAKE_EXE_LINKER_FLAGS="-static" \
        -DCMAKE_CXX_FLAGS="-static-libgcc -static-libstdc++" ..
  ```
- **Test Results**: 29/32 passed (3 expected failures = no .env config)
- **Files Changed**: CMake reconfigure (not CMakeLists.txt)

### 1. Fixed ClickHouse Source - Full 5-Level Depth
- Updated query to use correct column names: `bid_price_0..4`, `bid_size_0..4`, etc.
- Now reads ALL 5 depth levels (was only reading 1 level)
- Added OHLC and volume fields
- **Files Changed**: `src/datasource/clickhouse_source.cpp`

### 2. Updated TradeInfo Model for OI Data
- Added `oi_day_high`, `oi_day_low` fields (Kite FULL mode only)
- Added `average_traded_price` field
- Changed `oi_timestamp` → `exchange_timestamp`
- Added `has_oi()` helper method
- **Files Changed**: `include/core/models.hpp`

### 3. Created KiteWSDataSource Adapter
- New `KiteWSDataSource` implements `MarketDataSource` interface
- Wires Kite WebSocket into unified data pipeline
- Handles instrument_token ↔ exchange_token mapping
- OI data populated from live ticks
- **Files Added**: `src/datasource/kite_ws_source.cpp`
- **Files Changed**: `include/core/datasource.hpp`, `CMakeLists.txt`

### 4. Updated Kite WebSocket Snapshot Conversion
- Now populates `oi_day_high`, `oi_day_low` from tick data
- **Files Changed**: `src/kite/kite_websocket.cpp`

## CRITICAL ARCHITECTURE NOTES

### Data Sources
1. **ClickHouse** (`tick_data_db.market_data`): Historical tick data
   - Has: `instrument_id`, `tradingsymbol`, `expiry`, `strike`, 5-level depth
   - **NO** `open_interest` column - OI is Kite-live only
   - Query in screener uses `tradingsymbol LIKE 'NIFTY%'` for filtering
   
2. **Historical Instrument Tables** (to be used):
   - `instruments_nse` - NSE instrument master dumps
   - `instruments_nfo` - NFO instrument master dumps  
   - `xts_master` - XTS instrument master (partial)
   
3. **Live Instrument Source**:
   - Kite CSV files in `KITE_INSTRUMENT_MASTER_DIR`
   - Downloaded daily via `KiteClient::get_instruments()`

### OI Data Status - IMPORTANT
- **Kite Live (Full mode)**: ✅ Has OI (`oi`, `oi_day_high`, `oi_day_low`)
- **ClickHouse historical**: ❌ NO OI data (column doesn't exist)

### Data Source Usage Pattern
```cpp
// Historical replay - ClickHouse (no OI)
auto ch_source = create_clickhouse_source_from_config();
ch_source->replay_snapshots(symbols, range, callback);

// Live streaming - Kite WebSocket (with OI)
auto kite_source = create_kite_ws_source();
kite_source->subscribe(symbols);
kite_source->start_streaming(callback, error_callback);
```

### Option Chain Resolution
1. **Live**: `InstrumentManager::get_option_chain(underlying, expiry)` 
   - Uses `by_underlying_` multimap index
   - Filters by `is_option()` and optional `expiry`
   
2. **Historical**: Current screener queries `tradingsymbol LIKE 'NIFTY%'`
   - Works because `tradingsymbol` is denormalized in market_data
   - For proper join: Use `instruments_nfo` table

---

**PREVIOUS: Screener Service Added ✅**

All heavy computation handled server-side - frontend just renders:

#### REST Endpoints
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/screener/market` | GET | All instruments at timestamp (macro view) |
| `/api/screener/timestamps` | GET | Available timestamps in time range |
| `/api/screener/underlyings` | GET | Available underlyings (NIFTY, BANKNIFTY, etc.) |
| `/api/screener/expiries` | GET | Available expiries for underlying |
| `/api/screener/chain` | GET | Option chain (strike × calls/puts) |
| `/api/screener/iv-surface` | GET | IV surface (strike × DTE × IV) |
| `/api/screener/replay` | POST | Replay instruments over time range |

#### WebSocket Protocol
| Action | Direction | Description |
|--------|-----------|-------------|
| `start_replay` | Client→Server | Begin streaming replay |
| `replay_tick` | Server→Client | Each replay snapshot |
| `replay_progress` | Server→Client | Progress updates (0-100%) |
| `replay_complete` | Server→Client | Replay finished |
| `stop_replay` | Client→Server | Cancel replay |

#### Key Features
- **Pre-computed Greeks**: IV, Delta, Gamma, Theta, Vega for ALL options
- **Filtering**: By exchange, underlying, DTE, moneyness, IV, delta, spread
- **Sorting**: By any metric (volume, OI, IV, Greeks, spread, etc.)
- **Pagination**: offset + limit for large result sets
- **No Future Leakage**: Replay strictly `ORDER BY exchange_timestamp ASC`
- **Compare Mode**: Track prediction vs actual during replay

#### Files Added
- `include/screener/screener_service.hpp` - Service interface
- `src/screener/screener_service.cpp` - Implementation
- `src/api/screener_routes.cpp` - REST endpoints
- `src/api/screener_websocket.cpp` - WebSocket streaming

---

### Previous Session - January 17, 2026

**Session Goal:** Wire ClickHouse HTTP helper into codebase for streaming/replay instrument matching

**Completed Tasks:**
1. ✅ Refactored ClickHouseSource to use shared `clickhouse_http.hpp` helper
2. ✅ Added ClickHouse-backed instrument loader to InstrumentManager  
3. ✅ Updated main.cpp to load instruments at startup (directory first, CH fallback)
4. ✅ Fixed instrument_id ↔ exchange_token mapping for replay/streaming

**Key Architecture Changes:**

#### clickhouse_http.hpp/cpp Enhancements
- `ClickHouseHttpConfig::from_config()` - creates config from .env
- `clickhouse_parse_tsv()` - parses TSV response into rows
- `clickhouse_query_stream()` - streams results via callback
- `clickhouse_ping()` - connectivity check

#### InstrumentManager New Methods
- `load_from_clickhouse(database, table)` - loads from CH instrument dump
- `load_with_fallback()` - directory first, then ClickHouse fallback

#### Critical Insight: instrument_id = exchange_token
- ClickHouse `instrument_id` column = Kite `exchange_token`
- InstrumentManager uses `exchange_token` as primary lookup key
- Replay/streaming matches data `instrument_id` → instrument `exchange_token`
- This enables proper symbol resolution, lot sizes, etc.

---

### PREVIOUS BUILD - January 5, 2026

**Build Results:**
- `payoff_engine.exe` (789 KB) - Main executable
- `test_integration.exe` (735 KB) - Test suite

**Test Results:** 33/33 PASSED

Notes:
- ClickHouse connect fixed by URL-encoding credentials (handles '#') and ensuring Winsock init.
- Kite can now auto-fetch `KITE_ACCESS_TOKEN` from `KITE_ACCESS_TOKEN_URL` (credential service) in pure C++.

---

## ✅ FULLY IMPLEMENTED & VERIFIED

#### Core Infrastructure
1. **Config System** - config.hpp loads .env correctly
2. **HTTP Client (WinHTTP)** - http_client.cpp with REAL HTTPS using Windows WinHTTP
3. **SHA256 (BCrypt)** - Windows BCrypt API for token generation
4. **InstrumentManager** - Full CSV loading, O(1) lookups, option chain queries
   - **NEW**: ClickHouse fallback loading, load_with_fallback() strategy

#### ClickHouse Integration
5. **ClickHouse HTTP Helper** - Shared utility in clickhouse_http.hpp/cpp
   - URL-encoded credentials, TSV parsing, streaming callbacks
6. **ClickHouse Source** - Uses shared helper, proper instrument_id mapping
7. **PayoffReplayEngine** - Historical replay with instrument resolution

#### Kite Integration
8. **Kite REST Client** - make_request() uses HttpClient for REAL API calls
9. **Kite Token Auth** - generate_access_token() with SHA256 checksum
10. **Kite Token Service** - fetch access token from `KITE_ACCESS_TOKEN_URL`
11. **Kite WebSocket** - WinHTTP WebSocket API (wss://ws.kite.trade)
12. **Basket Margins API** - basket_margins() and order_margin()

#### Market Data
13. **Bar Builder** - Full OHLCV resampling (93 lines)

#### Pricing & Payoff
14. **Black-Scholes Pricing** - norm_cdf, d1/d2 (pricing.cpp)
15. **Greeks Calculation** - Delta, Gamma, Theta, Vega, Rho
16. **IV Calculation** - Newton-Raphson with Brenner-Subrahmanyam
17. **Payoff Calculator** - Strategies, curves, breakevens

#### Sensitivity & Risk
18. **Sensitivity Surfaces** - IV, Delta/Gamma/Theta/Vega surfaces
19. **P&L Sensitivity** - What-if analysis across dimensions
20. **VaR Calculator** - Monte Carlo VaR with correlated shocks
21. **Position Risk** - Margin, notional, exposure metrics
22. **Kill Switch** - Position limits, circuit breakers
23. **Drawdown Calculator** - Max drawdown, recovery factor

#### Streaming & Features
24. **Features Engine** - midprice, spread, imbalance, microprice
25. **Hint Engine** - staleness, spread, depth checks
26. **Streaming Pipeline** - dedup, gap detection, assembly
27. **WebSocket Server** - broadcast, rooms, JSON protocol

---

## 📋 PROJECT GOALS & OBJECTIVES

### Primary Goal
Port the Python DeskMetrics/Market-observatory payoff engine to production-grade C++ with:
- Sub-millisecond latency for real-time analytics
- No external dependencies beyond standard Windows APIs
- Full feature parity with Python implementation

### Key Objectives
1. **Real-time Options Pricing** - BS model, Greeks, IV calculation
2. **Market Data Streaming** - Kite WebSocket + ClickHouse replay
3. **Instrument Management** - 30K+ instruments with O(1) lookups
4. **Risk Management** - VaR, drawdown, kill switches
5. **Payoff Analysis** - Strategy P&L curves, breakevens, what-if

### Data Flow
```
Kite WS / ClickHouse  →  Streaming Pipeline  →  Features Engine  →  Payoff Calculator
         ↓                      ↓                     ↓                    ↓
   InstrumentManager      Dedup/Gap Detect      Execution Hints      Risk Metrics
```

### Config Keys (from .env)
- `CH_HOST`, `CH_PORT`, `CH_DATABASE`, `CH_TABLE` - ClickHouse
- `KITE_API_KEY`, `KITE_API_SECRET`, `KITE_ACCESS_TOKEN` - Kite
- `KITE_INSTRUMENT_MASTER_DIR` - Local instrument CSVs (fallback to CH)

---

## Build & Run

### Build Requirements
- MSYS2 MINGW64 with g++ 15.2.0
- CMake 4.2.1+ with Ninja
- Windows SDK (for WinHTTP, BCrypt)

### To Build
```bash
cd "C:\Users\LENOVO\Desktop\Payoff-Engine\cpp\build"
cmake --build .
```

### To Run
```bash
./payoff_engine.exe --test-connectivity
./payoff_engine.exe --clickhouse-ping
./test_integration.exe
```

### Environment
- ClickHouse: 110.172.21.62:8123, tick_data_db, market_data
- Kite: api_key=za4r7gn8aqq3tnwb, user=LEY228
