# Payoff Engine (C++)

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.20+-green.svg)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20|%20Linux-lightgrey.svg)]()

A **production-grade options analytics engine** built in C++20 for **live trading** and **deterministic historical simulation**. The system computes payoff curves, implied volatility (IV), Greeks, and risk analytics with **no lookahead bias** in replay mode.

## Features

- **Unified Code Path** — Same logic for live streaming and historical replay
- **No Future Data Leakage** — Strict watermark enforcement in simulations
- **Multi-Source Support** — ClickHouse (replay), Kite WebSocket (live), Mock (testing)
- **Lock-Free Architecture** — Zero-copy hot path with shared market data cache
- **Full Greeks Suite** — Delta, Gamma, Theta, Vega, Rho with Black-Scholes baseline
- **Payoff Curves** — Multi-leg strategy payoff visualization
- **Sensitivity Analysis** — Curvature maps and scenario runners
- **REST + WebSocket API** — Real-time streaming and on-demand queries

## Project Structure

```
cpp/
├── CMakeLists.txt           # Build configuration
├── include/                 # Public headers
│   ├── api/                 # REST/WebSocket server interfaces
│   ├── cache/               # Lock-free market data cache
│   ├── core/                # Models, datasource, config
│   ├── execution/           # Hint engine (PASSIVE|AGGRESSIVE|WAIT)
│   ├── features/            # Feature computation engine
│   ├── kite/                # Kite API client & WebSocket
│   ├── payoff/              # Pricing, Greeks, calculator
│   ├── resampling/          # Bar builder for OHLC
│   ├── screener/            # Options screener service
│   └── streaming/           # Pipeline, assembler, dedup
├── src/                     # Implementation files
│   ├── main.cpp             # Entry point
│   ├── api/                 # HTTP routes, WebSocket handlers
│   ├── core/                # Core implementations
│   ├── datasource/          # ClickHouse & Mock sources
│   ├── kite/                # Kite client implementation
│   ├── payoff/              # Pricing & Greeks calculations
│   └── streaming/           # Real-time data pipeline
└── tests/                   # Test suite
    ├── test_features.cpp
    ├── test_integration.cpp
    ├── test_models.cpp
    ├── test_parity.cpp
    ├── test_payoff.cpp
    └── test_streaming.cpp
```

## Prerequisites

- **Compiler**: GCC 11+, Clang 14+, or MSVC 2022+
- **CMake**: 3.20 or higher
- **vcpkg** (recommended): For dependency management

### Dependencies

| Library | Purpose | Required |
|---------|---------|----------|
| [nlohmann/json](https://github.com/nlohmann/json) | JSON serialization | Yes |
| [spdlog](https://github.com/gabime/spdlog) | Structured logging | Yes |
| [Google Test](https://github.com/google/googletest) | Unit testing | Optional |

## Quick Start

### 1. Clone and Setup

```bash
cd cpp
```

### 2. Install Dependencies (vcpkg)

```bash
# Install vcpkg if not already installed
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.bat  # Windows
./vcpkg/bootstrap-vcpkg.sh   # Linux/macOS

# Install dependencies
vcpkg install nlohmann-json spdlog gtest
```

### 3. Build

```bash
# Configure with CMake
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release

# Or with Ninja (faster)
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

### 4. Run

```bash
# Run the engine
./build/payoff_engine

# Run tests
ctest --test-dir build --output-on-failure
```

## Usage

### Command Line Options

```bash
payoff_engine [OPTIONS]

Options:
  --test-connectivity              Test all data source connections
  --kite-login-url                 Get Kite login URL
  --kite-exchange-request-token    Exchange request token for access token
  --clickhouse-ping                Test ClickHouse connectivity
```

### Example: Bull Call Spread

```cpp
#include "payoff/models.hpp"
#include "payoff/calculator.hpp"

using namespace payoff::engine;

// Create strategy
Strategy strategy;
strategy.name = "Bull Call Spread";

// Long lower strike call
OptionLeg long_call;
long_call.type = OptionType::Call;
long_call.side = Side::Buy;
long_call.strike = 20000.0;
long_call.quantity = 1;
long_call.premium = 250.0;
strategy.legs.push_back(long_call);

// Short higher strike call
OptionLeg short_call;
short_call.type = OptionType::Call;
short_call.side = Side::Sell;
short_call.strike = 20500.0;
short_call.quantity = 1;
short_call.premium = 100.0;
strategy.legs.push_back(short_call);

// Calculate payoff curve
PayoffCalculator calc;
auto curve = calc.compute_payoff(strategy, 19000.0, 21000.0, 50);
```

## Architecture

### Data Flow

```
Raw Feed (ClickHouse/Kite WS)
    ↓
Source Adapter (per-source normalization)
    ↓
Snapshot Assembler (merge partial updates)
    ↓
Dedup/Order (drop duplicates, enforce time order)
    ↓
Feature Engine (compute indicators)
    ↓
Execution Hint Engine (PASSIVE|AGGRESSIVE|WAIT)
    ↓
API Layer (REST + WebSocket)
    ↓
Frontend (render only)
```

### Core Models

**DepthSnapshot** — Canonical market data contract:
- `bids`: Sorted DESC (best bid first)
- `asks`: Sorted ASC (best ask first)  
- Invariant: `best_bid < best_ask`

**Strategy** — Multi-leg options position:
- Collection of `OptionLeg` (calls/puts, long/short)
- Supports common strategies: spreads, straddles, iron condors

**Greeks** — Risk sensitivities:
- Delta, Gamma, Theta, Vega, Rho
- Black-Scholes baseline pricing

### Libraries

| Library | Description |
|---------|-------------|
| `payoff_core` | Core models, data sources, streaming pipeline |
| `payoff_api` | REST server, WebSocket handlers, DTOs |

## Testing

```bash
# Run all tests
ctest --test-dir build

# Run specific test
./build/test_payoff

# Run with verbose output
ctest --test-dir build -V
```

### Test Categories

| Test | Purpose |
|------|---------|
| `test_models` | Data contract invariants |
| `test_features` | Feature computation correctness |
| `test_payoff` | Pricing & Greeks accuracy |
| `test_streaming` | Pipeline, dedup, assembly |
| `test_parity` | Replay vs live produce identical results |
| `test_integration` | End-to-end system tests |

## Configuration

Environment variables:

| Variable | Description | Default |
|----------|-------------|---------|
| `CLICKHOUSE_HOST` | ClickHouse server hostname | `localhost` |
| `CLICKHOUSE_PORT` | ClickHouse HTTP port | `8123` |
| `CLICKHOUSE_DATABASE` | Database name | `market_data` |
| `KITE_API_KEY` | Kite Connect API key | — |
| `KITE_API_SECRET` | Kite Connect API secret | — |
| `LIVE_MODE` | `clickhouse`, `kite`, `failover` | `clickhouse` |

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/symbols` | GET | List available symbols |
| `/replay/{symbol}` | GET | Historical data replay |
| `/stream/{symbol}` | WS | Real-time market data |
| `/payoff/calculate` | POST | Compute payoff curve |
| `/greeks` | POST | Calculate Greeks |
| `/screener` | GET | Options screener |

## Non-Negotiables

These are **hard requirements** — violations are bugs:

1. **Replay and streaming use identical downstream logic**
2. **Frontend never computes features** (render-only; backend owns math)
3. **Backend is data-source agnostic** (all feeds normalize to `DepthSnapshot`)
4. **Append-only, time-ordered data**
5. **Safe degradation**: returning `WAIT` is always valid
6. **No DB calls on the hot path**

## License

See the root [LICENSE](../LICENSE) file for details.

## Related Documentation

- [Architecture](../docs/ARCHITECTURE.md) — System design and module boundaries
- [Data Contract](../docs/DATA_CONTRACT.md) — Canonical data shapes and invariants
- [API Schema](../docs/API_DTO_SCHEMA.md) — JSON payloads for API integration
- [Build & Run](../docs/BUILD_AND_RUN.md) — Detailed build instructions
