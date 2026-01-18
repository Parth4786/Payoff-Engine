# Payoff Engine - Data Flow Documentation

> **Purpose**: Detailed documentation of data pipelines and transformation flows for frontend integration.

## Table of Contents

1. [Market Data Pipeline](#1-market-data-pipeline)
2. [Payoff Calculation Flow](#2-payoff-calculation-flow)
3. [Screener Data Flow](#3-screener-data-flow)
4. [Real-time Streaming Flow](#4-real-time-streaming-flow)
5. [Data Transformation Chains](#5-data-transformation-chains)

---

## 1. Market Data Pipeline

### 1.1 Overview

All market data flows through a **unified pipeline** regardless of source:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         RAW DATA SOURCES                                    │
├──────────────────┬────────────────────┬────────────────────────────────────┤
│   ClickHouse     │    Kite WebSocket  │         Mock Source                │
│   (Historical)   │    (Live)          │         (Testing)                  │
│                  │                    │                                    │
│   Wide rows      │   Binary packets   │   Generated snapshots              │
│   bid_price_0..4 │   instrument_token │                                    │
│   ask_price_0..4 │   depth arrays     │                                    │
└────────┬─────────┴──────────┬─────────┴─────────────────┬──────────────────┘
         │                    │                           │
         ▼                    ▼                           ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        SOURCE ADAPTERS                                      │
│                                                                             │
│   • Normalize to DepthSnapshot                                              │
│   • Map instrument_token → exchange_token (Kite only)                       │
│   • Tag with source type                                                    │
│   • Validate basic structure                                                │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        SNAPSHOT ASSEMBLER                                   │
│                                                                             │
│   • Merge partial updates (bids only / asks only)                           │
│   • Maintain last-known-good state per symbol                               │
│   • Mark is_partial flag when appropriate                                   │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        DEDUP & ORDERING                                     │
│                                                                             │
│   • Drop duplicate snapshots (same timestamp + content)                     │
│   • Reject time regressions (older than last seen)                          │
│   • Enforce strict ascending timestamp order                                │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        VALIDATION                                           │
│                                                                             │
│   Invariants checked:                                                       │
│   ✓ bids.length >= 1 && asks.length >= 1                                    │
│   ✓ bids sorted DESC by price                                               │
│   ✓ asks sorted ASC by price                                                │
│   ✓ best_bid < best_ask (no crossed book)                                   │
│                                                                             │
│   If invalid: drop snapshot, log error, continue                            │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                     CANONICAL DepthSnapshot                                 │
│                                                                             │
│   Ready for downstream consumption:                                         │
│   • Feature Engine                                                          │
│   • Screener Service                                                        │
│   • API Layer                                                               │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Source-Specific Transformations

#### ClickHouse → DepthSnapshot

```
ClickHouse Row                           DepthSnapshot
─────────────                            ─────────────
instrument_id      ─────────────────────→ instrument_id
"NFO:" + instrument_id ─────────────────→ symbol
capture_time       ─────────────────────→ exchange_timestamp

bid_price_0..4     ─┐
bid_size_0..4      ─┼──────────────────→ bids[] (sorted DESC)
bid_orders_0..4    ─┘

ask_price_0..4     ─┐
ask_size_0..4      ─┼──────────────────→ asks[] (sorted ASC)
ask_orders_0..4    ─┘

last_price         ─┐
last_traded_qty    ─┼──────────────────→ trade {}
volume, oi, etc.   ─┘

"clickhouse"       ─────────────────────→ source
false              ─────────────────────→ is_partial
(calculated)       ─────────────────────→ is_stale
```

#### Kite WebSocket → DepthSnapshot

```
Kite WS Packet                           DepthSnapshot
──────────────                           ─────────────
instrument_token   ──[lookup]───────────→ exchange_token → instrument_id
exchange:exchange_token ────────────────→ symbol

(packet timestamp) ─────────────────────→ exchange_timestamp

depth.buy[]        ─────────────────────→ bids[] (re-sort DESC)
depth.sell[]       ─────────────────────→ asks[] (re-sort ASC)

last_price, etc.   ─────────────────────→ trade {}

"kite_ws"          ─────────────────────→ source
(if partial update) ────────────────────→ is_partial
(if stale)         ─────────────────────→ is_stale
```

### 1.3 Identity Mapping (Critical)

**NEVER confuse these tokens:**

| Term | Description | Usage |
|------|-------------|-------|
| `instrument_token` | Kite's internal ID | Used for WS subscription |
| `exchange_token` | Exchange's ID | Used as `instrument_id` everywhere |
| `canonical_symbol` | `{exchange}:{exchange_token}` | Used in API responses |

**Mapping Flow:**
```
User Input: "NIFTY25JAN26300CE"
     │
     ▼
InstrumentManager.lookup(tradingsymbol)
     │
     ▼
InstrumentInfo {
  instrument_token: 408065,     // For Kite WS subscription
  exchange_token: 49543,        // = instrument_id
  tradingsymbol: "NIFTY25JAN26300CE",
  exchange: NFO
}
     │
     ▼
canonical_symbol = "NFO:49543"  // Used in all API responses
```

---

## 2. Payoff Calculation Flow

### 2.1 Strategy Analysis Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          FRONTEND INPUT                                     │
│                                                                             │
│  POST /api/payoff/calculate                                                 │
│  {                                                                          │
│    "underlying": "NIFTY",                                                   │
│    "spot": 26300,                                                           │
│    "legs": [...],                                                           │
│    "scenario": {...}                                                        │
│  }                                                                          │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        STRATEGY PARSER                                      │
│                                                                             │
│  • Parse leg JSON → OptionLeg structs                                       │
│  • Validate: strike > 0, quantity > 0, lot_size > 0                         │
│  • Create Strategy object                                                   │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      PAYOFF CALCULATOR                                      │
│                                                                             │
│  For each spot in [spot - 20%, spot + 20%]:                                 │
│    │                                                                        │
│    ├─→ For each leg:                                                        │
│    │     │                                                                  │
│    │     ├─→ If scenario.days_forward >= dte:                               │
│    │     │     └─→ calculate_expiry_pnl(leg, spot)                          │
│    │     │                                                                  │
│    │     └─→ Else:                                                          │
│    │           └─→ calculate_pnl_with_greeks(leg, spot, iv, time)           │
│    │                 │                                                      │
│    │                 └─→ PricingEngine.bs_price()                           │
│    │                                                                        │
│    └─→ Sum leg P&Ls → total_pnl at this spot                                │
│                                                                             │
│  Result: PayoffPoint[] array                                                │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      POST-PROCESSING                                        │
│                                                                             │
│  • Find max_profit = max(points[].pnl)                                      │
│  • Find max_loss = min(points[].pnl)                                        │
│  • Find breakevens (points where pnl crosses 0)                             │
│  • Calculate aggregate Greeks at current spot                               │
│  • Calculate POP (probability of profit)                                    │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      JSON RESPONSE                                          │
│                                                                             │
│  {                                                                          │
│    "max_profit": 5000,                                                      │
│    "max_loss": -2500,                                                       │
│    "breakevens": [26400],                                                   │
│    "greeks": {...},                                                         │
│    "points": [{"spot": 25000, "pnl": -2500}, ...]                           │
│  }                                                                          │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Pricing Engine Details

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        PRICING ENGINE                                       │
│                                                                             │
│  Inputs:                                                                    │
│    S  = spot price                                                          │
│    K  = strike price                                                        │
│    T  = time to expiry (years)                                              │
│    σ  = implied volatility                                                  │
│    r  = risk-free rate (default: 0.07)                                      │
│    q  = dividend yield (default: 0)                                         │
│                                                                             │
│  Black-Scholes Formula:                                                     │
│                                                                             │
│    d1 = [ln(S/K) + (r - q + σ²/2)T] / (σ√T)                                 │
│    d2 = d1 - σ√T                                                            │
│                                                                             │
│    Call = S·e^(-qT)·N(d1) - K·e^(-rT)·N(d2)                                 │
│    Put  = K·e^(-rT)·N(-d2) - S·e^(-qT)·N(-d1)                               │
│                                                                             │
│  Greeks:                                                                    │
│    Delta (Call) = e^(-qT)·N(d1)                                             │
│    Delta (Put)  = e^(-qT)·[N(d1) - 1]                                       │
│    Gamma = e^(-qT)·n(d1) / (S·σ·√T)                                         │
│    Theta = -(S·σ·e^(-qT)·n(d1))/(2√T) - r·K·e^(-rT)·N(d2) [call]            │
│    Vega  = S·e^(-qT)·√T·n(d1)                                               │
│    Rho   = K·T·e^(-rT)·N(d2) [call]                                         │
│                                                                             │
│  IV Solver (Newton-Raphson):                                                │
│    Given market price P, find σ such that BS(σ) = P                         │
│    σ_{n+1} = σ_n - (BS(σ_n) - P) / Vega(σ_n)                                │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.3 Expiry Payoff Calculation

```
For Call Option:
  payoff = max(0, spot - strike)
  P&L = (payoff - premium) × quantity × lot_size × side_multiplier
  
  where side_multiplier = +1 for BUY, -1 for SELL

For Put Option:
  payoff = max(0, strike - spot)
  P&L = (payoff - premium) × quantity × lot_size × side_multiplier

Example (Bull Call Spread):
  Spot = 26500
  
  Long 26300 CE @ 250:
    payoff = max(0, 26500 - 26300) = 200
    P&L = (200 - 250) × 1 × 25 × 1 = -1250
  
  Short 26500 CE @ 150:
    payoff = max(0, 26500 - 26500) = 0
    P&L = (0 - 150) × 1 × 25 × (-1) = +3750
  
  Total P&L = -1250 + 3750 = +2500
```

---

## 3. Screener Data Flow

### 3.1 Market Snapshot Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          FRONTEND REQUEST                                   │
│                                                                             │
│  GET /api/screener/market?timestamp=1767521472123&underlyings=NIFTY         │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      SCREENER SERVICE                                       │
│                      get_market_at_timestamp()                              │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
         ┌─────────────────────────┼─────────────────────────┐
         │                         │                         │
         ▼                         ▼                         ▼
┌─────────────────┐  ┌─────────────────────────┐  ┌─────────────────────────┐
│ INSTRUMENT      │  │     CLICKHOUSE          │  │    UNDERLYING           │
│ MANAGER         │  │     QUERY               │  │    LOOKUP               │
│                 │  │                         │  │                         │
│ Load all NFO    │  │ SELECT * FROM           │  │ Get spot prices for     │
│ instruments for │  │ market_data             │  │ NIFTY, BANKNIFTY, etc.  │
│ NIFTY           │  │ WHERE capture_time =    │  │                         │
│                 │  │   closest_to(timestamp) │  │                         │
│ Returns:        │  │ AND instrument_id IN    │  │                         │
│ • strike        │  │   (instrument_ids)      │  │                         │
│ • expiry        │  │                         │  │                         │
│ • lot_size      │  │ Returns:                │  │                         │
│ • tick_size     │  │ • price data            │  │                         │
│                 │  │ • depth                 │  │                         │
│                 │  │ • OI                    │  │                         │
└────────┬────────┘  └────────────┬────────────┘  └───────────┬─────────────┘
         │                        │                           │
         └────────────────────────┼───────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      ENRICHMENT (Per Instrument)                            │
│                                                                             │
│  For each instrument:                                                       │
│    │                                                                        │
│    ├─→ Calculate days_to_expiry                                             │
│    │                                                                        │
│    ├─→ Calculate moneyness = (spot - strike) / strike                       │
│    │                                                                        │
│    ├─→ Determine ITM/ATM/OTM:                                               │
│    │     • ATM: |moneyness| < 0.01 (within 1%)                              │
│    │     • ITM: Call + spot > strike, Put + spot < strike                   │
│    │     • OTM: otherwise                                                   │
│    │                                                                        │
│    ├─→ Calculate IV (if has_trade && price > 0):                            │
│    │     └─→ PricingEngine.calculate_iv(price, spot, strike, T)             │
│    │                                                                        │
│    ├─→ Calculate Greeks (if IV calculated successfully):                    │
│    │     └─→ PricingEngine.calculate_greeks(spot, strike, T, IV)            │
│    │                                                                        │
│    └─→ Calculate spread metrics:                                            │
│          spread = ask - bid                                                 │
│          spread_pct = spread / mid * 100                                    │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      FILTERING & SORTING                                    │
│                                                                             │
│  Apply filters:                                                             │
│    • include_calls / include_puts                                           │
│    • min_dte / max_dte                                                      │
│    • only_itm / only_atm / only_otm                                         │
│    • min_volume / min_oi                                                    │
│    • min_iv / max_iv                                                        │
│    • max_spread_pct                                                         │
│                                                                             │
│  Sort by specified field (volume, oi, iv, delta, etc.)                      │
│                                                                             │
│  Apply pagination (offset, limit)                                           │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      JSON RESPONSE                                          │
│                                                                             │
│  {                                                                          │
│    "timestamp": 1767521472123,                                              │
│    "underlyings": [...],                                                    │
│    "instruments": [...],                                                    │
│    "total_instruments": 450,                                                │
│    "query_time_ms": 45.2,                                                   │
│    "calc_time_ms": 123.5                                                    │
│  }                                                                          │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Option Chain Assembly

```
Raw Instrument List          Option Chain Structure
────────────────────         ──────────────────────
[
  {id: 101, strike: 26200, type: CE, ...},     │
  {id: 102, strike: 26200, type: PE, ...},  ───┼──→  {strike: 26200, call: {...}, put: {...}}
  {id: 103, strike: 26300, type: CE, ...},     │
  {id: 104, strike: 26300, type: PE, ...},  ───┼──→  {strike: 26300, call: {...}, put: {...}}
  {id: 105, strike: 26400, type: CE, ...},     │
  {id: 106, strike: 26400, type: PE, ...},  ───┴──→  {strike: 26400, call: {...}, put: {...}}
]

Algorithm:
1. Group instruments by strike
2. For each strike group:
   - Find CE instrument → call
   - Find PE instrument → put
   - Calculate net_oi = call.oi - put.oi
   - Calculate net_volume = call.volume - put.volume
3. Sort by strike ascending
4. Identify ATM strike (closest to spot)
5. Calculate max pain (strike with minimum total payout)
```

---

## 4. Real-time Streaming Flow

### 4.1 WebSocket Connection Flow

```
┌─────────────────┐                                    ┌─────────────────┐
│    FRONTEND     │                                    │    BACKEND      │
│   (Browser)     │                                    │  (C++ Server)   │
└────────┬────────┘                                    └────────┬────────┘
         │                                                      │
         │  ──────────── WebSocket Connect ────────────────────→│
         │                                                      │
         │  ←───────────── Connected ──────────────────────────│
         │                                                      │
         │  {"action":"subscribe","symbols":["NFO:49543"]} ───→│
         │                                                      │
         │                                    ┌─────────────────┴─────────────────┐
         │                                    │ Subscribe to Kite WS              │
         │                                    │ OR query ClickHouse               │
         │                                    └─────────────────┬─────────────────┘
         │                                                      │
         │  ←──────── {"type":"subscribed"} ───────────────────│
         │                                                      │
         │  ←──────── Market Data Stream ──────────────────────│
         │  {"type":"depth_snapshot", "symbol":"NFO:49543"...} │
         │                                                      │
         │  {"action":"unsubscribe","symbols":["NFO:49543"]} ─→│
         │                                                      │
         │  ←──────── {"type":"unsubscribed"} ─────────────────│
         │                                                      │
```

### 4.2 Streaming Data Transformation

```
Source (Kite WS / Replay)
         │
         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      STREAMING PIPELINE                                     │
│                                                                             │
│  1. Raw packet → DepthSnapshot                                              │
│  2. Assembler: merge partial updates                                        │
│  3. Dedup: drop duplicates                                                  │
│  4. Validation: check invariants                                            │
│  5. Feature Engine: compute features                                        │
│  6. Hint Engine: compute execution posture                                  │
│  7. Rate Limiter: throttle to client capacity                               │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
         ┌─────────────────────────┴─────────────────────────┐
         │                                                   │
         ▼                                                   ▼
┌─────────────────────────┐                    ┌─────────────────────────────┐
│    Market Data Stream   │                    │    Feature/Hint Stream      │
│                         │                    │                             │
│  {                      │                    │  {                          │
│    "type": "depth",     │                    │    "type": "features",      │
│    "symbol": "...",     │                    │    "symbol": "...",         │
│    "bids": [...],       │                    │    "midprice": 285.50,      │
│    "asks": [...],       │                    │    "spread_bps": 3.5,       │
│    "trade": {...}       │                    │    "imbalance": 0.35,       │
│  }                      │                    │    "hint": {                │
│                         │                    │      "posture": "PASSIVE",  │
│                         │                    │      "reasons": [...]       │
│                         │                    │    }                        │
│                         │                    │  }                          │
└─────────────────────────┘                    └─────────────────────────────┘
```

---

## 5. Data Transformation Chains

### 5.1 Full Request-Response Chains

#### Payoff Calculate Chain
```
Request JSON → parse_legs_from_json() → vector<OptionLeg>
    → Strategy{legs, underlying, spot}
    → PayoffCalculator.calculate_scenario_payoff()
    → PayoffCurve
    → payoff_curve_to_json()
    → Response JSON
```

#### Greeks Calculate Chain
```
Request JSON → json_get_double(spot, strike, iv, dte)
    → PricingParams{S, K, T, σ, r}
    → calculate_greeks(params, type)
    → Greeks{delta, gamma, theta, vega, rho}
    → greeks_to_json()
    → Response JSON
```

#### Screener Market Chain
```
Query params → ScreenerFilter
    → ClickHouse query (bulk fetch)
    → vector<DepthSnapshot>
    → Enrich: compute IV, Greeks per instrument
    → Filter & Sort
    → Paginate
    → MarketScreenerResult
    → serialize_instruments()
    → Response JSON
```

### 5.2 Type Conversion Summary

| From | To | Function |
|------|-----|----------|
| JSON leg object | `OptionLeg` | `parse_legs_from_json()` |
| `OptionLeg` | JSON | `option_leg_to_json()` |
| `Strategy` | JSON | `strategy_to_json()` |
| `Greeks` | JSON | `greeks_to_json()` |
| `PayoffCurve` | JSON | `payoff_curve_to_json()` |
| `InstrumentSnapshot` | JSON | `serialize_instrument()` |
| ClickHouse row | `DepthSnapshot` | Source adapter |
| Kite WS packet | `DepthSnapshot` | Source adapter |

---

## 🔗 Related Documents

- [DATA_SCHEMA.md](./DATA_SCHEMA.md) - Data type definitions
- [API_REFERENCE.md](./API_REFERENCE.md) - Endpoint specifications
- [FRONTEND_INTEGRATION.md](./FRONTEND_INTEGRATION.md) - Integration guide
