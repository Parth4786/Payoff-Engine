# Payoff Engine - Architecture Overview

> **Purpose**: This document provides a high-level architecture blueprint for frontend developers integrating with the Payoff Engine C++ backend.

## 🎯 System Purpose

The Payoff Engine is a professional-grade **Options Strategy Builder & Analytics Platform** that provides:

1. **Market Data Pipeline** - Real-time and historical market data processing
2. **Payoff Engine** - Options strategy P&L calculation with Greeks
3. **Market Screener** - Macro-to-micro analysis of the entire options market
4. **Execution Hints** - Intelligent trading posture recommendations

## 🏗️ Architecture Layers

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              FRONTEND (Render Only)                         │
│  • React/Vue/Svelte UI                                                     │
│  • Receives pre-computed data                                              │
│  • No business logic                                                       │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │ HTTP REST / WebSocket
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                                API LAYER                                    │
│  ┌─────────────────┐  ┌────────────────────┐  ┌─────────────────────────┐  │
│  │   REST Server   │  │  WebSocket Server  │  │   Screener Routes       │  │
│  │  /api/payoff/*  │  │  Real-time quotes  │  │  /api/screener/*        │  │
│  │  /api/greeks/*  │  │  Strategy updates  │  │  Market-wide queries    │  │
│  └─────────────────┘  └────────────────────┘  └─────────────────────────┘  │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            COMPUTATION LAYER                                │
│  ┌──────────────────┐  ┌────────────────┐  ┌──────────────────────────┐    │
│  │  Payoff Engine   │  │ Feature Engine │  │    Screener Service      │    │
│  │  • Curves        │  │ • Microstructure│ │  • Market snapshots      │    │
│  │  • Greeks        │  │ • Imbalance    │  │  • IV surface            │    │
│  │  • Scenarios     │  │ • Spread       │  │  • Option chains         │    │
│  │  • Sensitivities │  │ • OFI          │  │  • Replay                │    │
│  └──────────────────┘  └────────────────┘  └──────────────────────────┘    │
│                              │                                              │
│  ┌──────────────────┐  ┌─────────────────┐                                 │
│  │   Pricing        │  │  Execution      │                                 │
│  │  • Black-Scholes │  │  Hint Engine    │                                 │
│  │  • IV Solver     │  │  • WAIT/PASSIVE │                                 │
│  │  • Greeks calc   │  │  • AGGRESSIVE   │                                 │
│  └──────────────────┘  └─────────────────┘                                 │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            DATA LAYER                                       │
│  ┌──────────────────┐  ┌────────────────┐  ┌──────────────────────────┐    │
│  │  Data Sources    │  │  Streaming     │  │   Instrument Manager     │    │
│  │  • ClickHouse    │  │  Pipeline      │  │  • Token lookup          │    │
│  │  • Kite WS       │  │  • Assembler   │  │  • Symbol resolution     │    │
│  │  • Mock          │  │  • Dedup       │  │  • Lot/tick size         │    │
│  └──────────────────┘  │  • Gap detect  │  └──────────────────────────┘    │
│                        └────────────────┘                                   │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 🔑 Key Design Principles

### 1. Frontend is Render-Only
The frontend should **never** perform:
- Greeks calculations
- Payoff curve computation
- IV estimation
- Feature extraction

**All computation happens server-side.** The frontend receives pre-computed JSON payloads.

### 2. Source-Agnostic API
All data flows through a **canonical contract** (`DepthSnapshot`, `FeatureSnapshot`). The frontend doesn't know or care if data comes from:
- ClickHouse (historical replay)
- Kite WebSocket (live)
- Mock source (testing)

### 3. Explicit Uncertainty
Every data packet includes quality flags:
```json
{
  "source": "clickhouse | kite_ws | mock",
  "is_partial": false,
  "is_stale": false
}
```
Frontend should render warnings when `is_stale=true`.

### 4. Deterministic Outputs
Same inputs → same outputs. Every response includes assumptions for reproducibility:
```json
{
  "assumptions": {
    "risk_free_rate": 0.07,
    "iv_model": "black_scholes",
    "day_count": 365
  }
}
```

## 📦 Module Overview

| Module | Purpose | Frontend Interaction |
|--------|---------|---------------------|
| `payoff/` | Options strategy calculation | POST `/api/payoff/calculate` |
| `features/` | Market microstructure features | GET `/api/quote`, streaming |
| `screener/` | Market-wide analysis | GET `/api/screener/*` |
| `execution/` | Trading posture hints | Embedded in snapshots |
| `api/` | REST & WebSocket handlers | All HTTP/WS communication |
| `core/` | Data models, sources | Internal only |

## 🌊 Data Flow Summary

### Strategy Analysis Flow
```
Frontend (Strategy Input)
    │
    ▼
POST /api/payoff/calculate
    │
    ▼
PayoffCalculator.calculate_scenario_payoff()
    │
    ▼
PricingEngine (Black-Scholes + Greeks)
    │
    ▼
JSON Response (PayoffCurve + breakevens + Greeks)
    │
    ▼
Frontend (Render chart)
```

### Market Screener Flow
```
Frontend (Timestamp + Filters)
    │
    ▼
GET /api/screener/market?timestamp=...
    │
    ▼
ScreenerService.get_market_at_timestamp()
    │
    ├─→ ClickHouse (fetch all instruments at timestamp)
    ├─→ IV Calculation (per instrument)
    ├─→ Greeks Calculation (per instrument)
    │
    ▼
JSON Response (MarketScreenerResult with instruments[])
    │
    ▼
Frontend (Render table/heatmap)
```

### Real-time Streaming Flow
```
WebSocket Connection
    │
    ▼
Subscribe Message: {"action": "subscribe", "symbols": ["NFO:49543"]}
    │
    ▼
Server pushes updates:
    ├─→ Market ticks
    ├─→ Feature updates
    ├─→ Strategy P&L changes
    │
    ▼
Frontend (Live update UI)
```

## 📁 Related Documentation

| Document | Description |
|----------|-------------|
| [DATA_SCHEMA.md](./DATA_SCHEMA.md) | All data types and JSON contracts |
| [API_REFERENCE.md](./API_REFERENCE.md) | REST endpoint specifications |
| [DATA_FLOW.md](./DATA_FLOW.md) | Detailed data pipeline flows |
| [FRONTEND_INTEGRATION.md](./FRONTEND_INTEGRATION.md) | Integration guide for frontend |
| [WEBSOCKET_PROTOCOL.md](./WEBSOCKET_PROTOCOL.md) | Real-time streaming protocol |

## 🔧 Technology Stack

- **Backend**: C++17
- **HTTP Server**: Custom lightweight (httplib-style)
- **Database**: ClickHouse (columnar, optimized for time-series)
- **Live Feed**: Kite WebSocket (Zerodha)
- **Pricing**: Black-Scholes with Newton-Raphson IV solver
