# Payoff Engine - Data Schema Reference

> **Purpose**: Complete reference of all data types, JSON contracts, and wire formats for frontend integration.

## Table of Contents

1. [Core Market Data](#1-core-market-data)
2. [Option Strategy Models](#2-option-strategy-models)
3. [Payoff & Greeks](#3-payoff--greeks)
4. [Screener Data Types](#4-screener-data-types)
5. [Execution Hints](#5-execution-hints)
6. [Feature Snapshots](#6-feature-snapshots)
7. [Enums & Constants](#7-enums--constants)

---

## 1. Core Market Data

### 1.1 DepthLevel

Single price level in an order book.

```typescript
interface DepthLevel {
  price: number;      // Price at this level (> 0)
  size: number;       // Quantity (shares/contracts)
  orders: number;     // Number of orders at this level
}
```

**JSON Example:**
```json
{"price": 26300.50, "size": 1250, "orders": 5}
```

### 1.2 TradeInfo

Last trade and aggregate volume information.

```typescript
interface TradeInfo {
  last_price: number;
  last_qty: number;
  total_traded_quantity: number;
  average_traded_price: number;
  total_buy_quantity: number;
  total_sell_quantity: number;
  
  // Open Interest (options/futures)
  oi: number | null;
  oi_day_high: number | null;
  oi_day_low: number | null;
  
  // Timestamps (Unix seconds)
  last_trade_time_s: number | null;
  exchange_timestamp_s: number | null;
  
  // Daily OHLC
  ohlc: {
    open: number;
    high: number;
    low: number;
    close: number;
  };
}
```

### 1.3 DepthSnapshot

**The canonical market data contract.** All market data from any source normalizes to this shape.

```typescript
interface DepthSnapshot {
  type: "depth_snapshot";
  
  // Identity
  symbol: string;           // Canonical: "{exchange}:{exchange_token}"
  instrument_id: number;    // Exchange token (NOT instrument_token)
  
  // Timestamp (Unix milliseconds)
  capture_time_ms: number;
  
  // Order Book (5 levels)
  bids: DepthLevel[];       // MUST be sorted DESC by price (best first)
  asks: DepthLevel[];       // MUST be sorted ASC by price (best first)
  
  // Trade data
  trade: TradeInfo;
  
  // Quality metadata
  source: "clickhouse" | "kite_ws" | "mock";
  is_partial: boolean;      // Only bids OR asks updated
  is_stale: boolean;        // Data older than threshold
}
```

**JSON Example:**
```json
{
  "type": "depth_snapshot",
  "symbol": "NFO:49543",
  "instrument_id": 49543,
  "capture_time_ms": 1767521472123,
  
  "bids": [
    {"price": 26299.50, "size": 225, "orders": 1},
    {"price": 26299.00, "size": 450, "orders": 2}
  ],
  "asks": [
    {"price": 26300.00, "size": 975, "orders": 3},
    {"price": 26300.50, "size": 300, "orders": 1}
  ],
  
  "trade": {
    "last_price": 26299.75,
    "last_qty": 150,
    "total_traded_quantity": 15000000,
    "average_traded_price": 26250.00,
    "total_buy_quantity": 118425,
    "total_sell_quantity": 58425,
    "oi": null,
    "oi_day_high": null,
    "oi_day_low": null,
    "last_trade_time_s": 1767521470,
    "exchange_timestamp_s": 1767521470,
    "ohlc": {"open": 26100.00, "high": 26350.00, "low": 26050.00, "close": 26150.00}
  },
  
  "source": "clickhouse",
  "is_partial": false,
  "is_stale": false
}
```

**Invariants (enforced server-side):**
- `bids.length >= 1` and `asks.length >= 1`
- Bids strictly descending by price
- Asks strictly ascending by price
- `bids[0].price < asks[0].price` (no crossed book)

---

## 2. Option Strategy Models

### 2.1 OptionLeg

Single leg in an options strategy.

```typescript
interface OptionLeg {
  // Contract details
  type: "CE" | "PE";        // Call or Put
  side: "BUY" | "SELL";
  strike: number;
  expiry?: string;          // ISO date: "2026-01-30"
  
  // Position sizing
  quantity: number;         // In lots
  lot_size: number;         // Multiplier (e.g., 25 for NIFTY)
  premium: number;          // Entry price per unit
  
  // Optional: linked instrument
  instrument_id?: number;
  symbol?: string;
  
  // Computed (returned by server)
  total_qty: number;        // quantity × lot_size
  net_premium: number;      // Signed: negative for buy, positive for sell
}
```

**JSON Example:**
```json
{
  "type": "CE",
  "side": "BUY",
  "strike": 26300,
  "quantity": 1,
  "lot_size": 25,
  "premium": 250.00,
  "total_qty": 25,
  "net_premium": -6250.00
}
```

### 2.2 Strategy

Collection of option legs forming a strategy.

```typescript
interface Strategy {
  name: string;             // e.g., "Bull Call Spread"
  underlying: string;       // e.g., "NIFTY"
  underlying_price: number; // Current spot
  
  legs: OptionLeg[];
  
  // Computed (returned by server)
  total_premium: number;    // Net credit/debit
  is_credit: boolean;       // total_premium > 0
  is_debit: boolean;        // total_premium < 0
  leg_count: number;
}
```

**JSON Example:**
```json
{
  "name": "Bull Call Spread",
  "underlying": "NIFTY",
  "underlying_price": 26300,
  "legs": [
    {"type": "CE", "side": "BUY", "strike": 26300, "quantity": 1, "lot_size": 25, "premium": 250},
    {"type": "CE", "side": "SELL", "strike": 26500, "quantity": 1, "lot_size": 25, "premium": 150}
  ],
  "total_premium": -2500,
  "is_credit": false,
  "leg_count": 2
}
```

### 2.3 Scenario

What-if parameters for scenario analysis.

```typescript
interface Scenario {
  spot_shift_pct: number;   // % change in underlying (-10 to +10)
  iv_shift_pct: number;     // % change in IV (-50 to +50)
  days_forward: number;     // Days to advance (0 = today)
  description: string;      // Human-readable label
}
```

**Preset Scenarios:**
```json
[
  {"spot_shift_pct": 0, "iv_shift_pct": 0, "days_forward": 0, "description": "Today"},
  {"spot_shift_pct": 0, "iv_shift_pct": 0, "days_forward": 7, "description": "T+7"},
  {"spot_shift_pct": 0, "iv_shift_pct": 0, "days_forward": 365, "description": "At Expiry"},
  {"spot_shift_pct": 0, "iv_shift_pct": -20, "days_forward": 0, "description": "IV Crush -20%"}
]
```

---

## 3. Payoff & Greeks

### 3.1 Greeks

Option sensitivities (per leg or aggregate).

```typescript
interface Greeks {
  delta: number;    // ∂V/∂S
  gamma: number;    // ∂²V/∂S²
  theta: number;    // ∂V/∂t (per day, in ₹)
  vega: number;     // ∂V/∂σ (per 1% IV change, in ₹)
  rho: number;      // ∂V/∂r (per 1% rate change, in ₹)
}
```

**JSON Example:**
```json
{
  "delta": 0.5234,
  "gamma": 0.0012,
  "theta": -15.25,
  "vega": 42.50,
  "rho": 8.75
}
```

### 3.2 PayoffPoint

Single point on a payoff curve.

```typescript
interface PayoffPoint {
  spot: number;     // Underlying price at this point
  pnl: number;      // P&L at this spot (in ₹)
  pnl_pct?: number; // P&L as % of margin/premium
  greeks?: Greeks;  // Greeks at this spot (optional)
}
```

### 3.3 PayoffCurve

Complete payoff calculation result.

```typescript
interface PayoffCurve {
  scenario_name: string;
  
  // Curve data
  points: PayoffPoint[];
  
  // Summary statistics
  max_profit: number;         // Maximum possible profit
  max_loss: number;           // Maximum possible loss (negative)
  breakevens: number[];       // Breakeven spot prices
  
  // Risk metrics
  probability_of_profit: number;  // 0-1 POP estimate
  expected_value: number;         // Expected P&L
  tail_loss_5pct: number;         // 5th percentile loss
  tail_loss_1pct: number;         // 1st percentile loss
  
  // Current Greeks (aggregate at current spot)
  greeks: Greeks;
}
```

**JSON Example:**
```json
{
  "scenario_name": "At Expiry",
  "max_profit": 5000.00,
  "max_loss": -2500.00,
  "breakevens": [26350.00],
  "probability_of_profit": 0.42,
  "expected_value": 125.00,
  "greeks": {
    "delta": 0.48,
    "gamma": 0.0008,
    "theta": -25.50,
    "vega": 85.00,
    "rho": 12.00
  },
  "points": [
    {"spot": 25000, "pnl": -2500},
    {"spot": 26000, "pnl": -2500},
    {"spot": 26300, "pnl": -2500},
    {"spot": 26350, "pnl": 0},
    {"spot": 26500, "pnl": 5000},
    {"spot": 27000, "pnl": 5000}
  ]
}
```

### 3.4 SensitivitySurface

2D heatmap data for sensitivity analysis.

```typescript
interface SensitivityCell {
  x: number;              // X-axis value (e.g., spot)
  y: number;              // Y-axis value (e.g., time)
  value: number;          // Computed value (e.g., delta, P&L)
  is_kill_zone: boolean;  // High-risk region flag
}

interface SensitivitySurface {
  name: string;           // e.g., "Delta vs Spot×Time"
  x_label: string;        // "Spot Price"
  y_label: string;        // "Days to Expiry"
  value_label: string;    // "Delta"
  
  x_axis: number[];       // X-axis values
  y_axis: number[];       // Y-axis values
  grid: SensitivityCell[][];
  
  kill_zone_threshold: number;
}
```

---

## 4. Screener Data Types

### 4.1 InstrumentSnapshot

Market snapshot for a single instrument (used in screener results).

```typescript
interface InstrumentSnapshot {
  // Identity
  instrument_id: number;          // Exchange token
  tradingsymbol: string;          // e.g., "NIFTY25JAN26300CE"
  underlying: string;             // e.g., "NIFTY"
  exchange: string;               // "NFO" | "NSE" | "BFO"
  instrument_type: number;        // 0=Unknown, 1=EQ, 2=FUT, 3=CE, 4=PE
  
  // Option-specific
  strike: number;
  option_type: "CE" | "PE";
  expiry_ms: number;              // Unix timestamp of expiry
  days_to_expiry: number;
  
  // Price data
  last_price: number;
  bid_price: number;
  ask_price: number;
  mid_price: number;
  spread: number;                 // ask - bid
  spread_pct: number;             // spread / mid * 100
  
  // Volume/OI
  volume: number;
  open_interest: number;
  oi_change: number;              // Change from previous session
  
  // Greeks (pre-calculated by server)
  iv: number;                     // Implied volatility (0.15 = 15%)
  iv_pct: number;                 // IV as percentage (15.0)
  iv_percentile: number;          // IV rank 0-100
  delta: number;
  gamma: number;
  theta: number;
  vega: number;
  
  // Derived metrics
  moneyness: number;              // (spot - strike) / strike
  is_itm: boolean;
  is_atm: boolean;
  is_otm: boolean;
  
  // Timestamp
  timestamp: number;              // Unix ms
}
```

### 4.2 UnderlyingSnapshot

Index/equity summary for screener.

```typescript
interface UnderlyingSnapshot {
  symbol: string;                 // "NIFTY"
  spot_price: number;
  prev_close: number;
  change: number;
  change_pct: number;
  day_high: number;
  day_low: number;
  volume: number;
  
  // ATM IV
  atm_iv: number;
  atm_iv_percentile: number;
  
  // Option chain summary
  total_calls: number;
  total_puts: number;
  total_call_oi: number;
  total_put_oi: number;
  pcr_oi: number;                 // Put-Call Ratio by OI
  pcr_volume: number;             // Put-Call Ratio by Volume
}
```

### 4.3 MarketScreenerResult

Complete market state at a timestamp.

```typescript
interface MarketScreenerResult {
  timestamp: number;              // Unix ms
  
  underlyings: UnderlyingSnapshot[];
  instruments: InstrumentSnapshot[];
  
  // Statistics
  total_instruments: number;
  options_count: number;
  futures_count: number;
  equities_count: number;
  
  // Performance
  query_time_ms: number;
  calc_time_ms: number;
}
```

### 4.4 ScreenerFilter

Filter options for screener queries.

```typescript
interface ScreenerFilter {
  // Exchange filter
  exchanges?: string[];           // ["NFO", "BFO"]
  
  // Underlying filter
  underlyings?: string[];         // ["NIFTY", "BANKNIFTY"]
  
  // Instrument type
  include_options?: boolean;      // default: true
  include_futures?: boolean;      // default: true
  include_equities?: boolean;     // default: false
  include_calls?: boolean;        // default: true
  include_puts?: boolean;         // default: true
  
  // Expiry filter
  min_dte?: number;
  max_dte?: number;
  specific_expiry_ms?: number;
  
  // Moneyness filter
  only_itm?: boolean;
  only_atm?: boolean;
  only_otm?: boolean;
  
  // Volume/OI filter
  min_volume?: number;
  min_oi?: number;
  
  // Greek filters
  min_iv?: number;
  max_iv?: number;
  min_delta?: number;
  max_delta?: number;
  
  // Liquidity filter
  max_spread_pct?: number;        // e.g., 0.02 for 2%
  
  // Pagination
  offset?: number;                // default: 0
  limit?: number;                 // default: 100
  
  // Sorting
  sort_by?: string;               // "volume" | "oi" | "iv" | "delta" | etc.
  sort_order?: "asc" | "desc";    // default: "desc"
}
```

### 4.5 OptionChainResult

Structured option chain view.

```typescript
interface OptionChainEntry {
  strike: number;
  call: InstrumentSnapshot;
  put: InstrumentSnapshot;
  net_oi: number;                 // Call OI - Put OI
  net_volume: number;
}

interface OptionChainResult {
  underlying: string;
  spot_price: number;
  expiry_ms: number;
  timestamp: number;
  chain: OptionChainEntry[];
  atm_strike: number;
  max_pain: number;
}
```

### 4.6 IVSurfaceResult

IV surface for visualization.

```typescript
interface IVSurfacePoint {
  strike: number;
  dte: number;                    // Days to expiry
  iv: number;
}

interface IVSurfaceResult {
  underlying: string;
  spot_price: number;
  timestamp: number;
  surface: IVSurfacePoint[];
}
```

---

## 5. Execution Hints

### 5.1 ExecutionHint

Trading posture recommendation.

```typescript
interface ExecutionHint {
  posture: "WAIT" | "PASSIVE" | "AGGRESSIVE";
  reasons: string[];              // Human-readable explanations
  confidence: number;             // 0-1 confidence level
  
  // Feature values that drove the decision
  spread_bps: number;
  imbalance: number;
  depth_slope: number;
  shock: number;
}
```

**JSON Example:**
```json
{
  "posture": "PASSIVE",
  "reasons": [
    "TIGHT_SPREAD: Spread 3.5 bps < 5 bps threshold",
    "FAVORABLE_IMBALANCE: Bid imbalance 0.35 supports buy side"
  ],
  "confidence": 0.72,
  "spread_bps": 3.5,
  "imbalance": 0.35,
  "depth_slope": 0.12,
  "shock": 0.0
}
```

**Posture Meanings:**
| Posture | Meaning | Frontend Action |
|---------|---------|-----------------|
| `WAIT` | Do not trade - conditions unfavorable | Show warning, disable execute |
| `PASSIVE` | Post limit orders, wait for fill | Show green, enable execute |
| `AGGRESSIVE` | Cross spread, take liquidity | Show orange, show urgency |

---

## 6. Feature Snapshots

### 6.1 FeatureSnapshot

Market microstructure features (computed by Feature Engine).

```typescript
interface FeatureSnapshot {
  instrument_id: number;
  symbol: string;
  timestamp: number;              // Unix ms
  
  // Price metrics
  midprice: number;
  spread: number;
  spread_bps: number;             // Spread in basis points
  
  // Imbalance metrics
  bid_ask_imbalance: number;      // (bid_size - ask_size) / total
  microprice: number;             // Imbalance-weighted mid
  
  // Depth metrics
  bid_depth: number;              // Total bid size (all levels)
  ask_depth: number;
  depth_slope: number;            // How depth changes with price
  
  // Liquidity metrics
  lpi: number;                    // Liquidity Provider Index
  ofi: number;                    // Order Flow Imbalance
  
  // Volatility
  shock: number;                  // Price shock indicator
  
  // Quality flags
  is_stale: boolean;
  is_partial: boolean;
}
```

---

## 7. Enums & Constants

### 7.1 Source

Data source identifier.

```typescript
type Source = "clickhouse" | "kite_ws" | "mock";
```

### 7.2 OptionType

```typescript
type OptionType = "CE" | "PE";
```

### 7.3 Side

```typescript
type Side = "BUY" | "SELL";
```

### 7.4 InstrumentType

```typescript
enum InstrumentType {
  Unknown = 0,
  EQ = 1,      // Equity
  FUT = 2,     // Futures
  CE = 3,      // Call Option
  PE = 4       // Put Option
}
```

### 7.5 Exchange

```typescript
enum Exchange {
  Unknown = 0,
  NSE = 1,
  NFO = 2,
  BSE = 3,
  BFO = 4,
  CDS = 5,
  MCX = 6
}
```

### 7.6 ScreenerSortField

```typescript
type ScreenerSortField = 
  | "instrument_id"
  | "symbol"
  | "last_price"
  | "volume"
  | "open_interest"
  | "oi_change"
  | "iv"
  | "iv_percentile"
  | "delta"
  | "gamma"
  | "theta"
  | "vega"
  | "spread"
  | "spread_pct"
  | "days_to_expiry"
  | "moneyness";
```

---

## 📌 Key Validation Rules

| Field | Rule |
|-------|------|
| `symbol` | Format: `{exchange}:{exchange_token}` (e.g., `NFO:49543`) |
| `instrument_id` | Always use `exchange_token`, never `instrument_token` |
| `bids[]` | Must be sorted DESC by price |
| `asks[]` | Must be sorted ASC by price |
| `best_bid < best_ask` | No crossed books allowed |
| `is_stale` | If true, suppress fragile features (microprice, etc.) |
| `iv` | Decimal (0.15 = 15%), not percentage |

---

## 🔗 Related Documents

- [API_REFERENCE.md](./API_REFERENCE.md) - Endpoint specifications
- [FRONTEND_INTEGRATION.md](./FRONTEND_INTEGRATION.md) - Integration guide
