# API DTO Schema (C++ → Frontend)

This document defines the **wire-level JSON payloads** the C++ backend should expose.

Goals:

- frontend is render-only (no feature math)
- payloads are source-agnostic
- uncertainty is explicit (`source`, `is_partial`, `is_stale`)
- deterministic and reproducible outputs

## 1) Conventions

### 1.1 Time

Use one of the following (pick one and keep it consistent across all endpoints):

Option A (recommended for correctness):

- `capture_time_ms`: integer milliseconds since Unix epoch

Option B (human-friendly):

- `capture_time`: ISO-8601 string in UTC (e.g. `2026-01-04T10:11:12.123Z`)

### 1.2 Identity

- `symbol` is the canonical symbol: `"{exchange}:{exchange_token}"`.
- `instrument_id` is the canonical numeric id (the exchange token).

### 1.3 Source

- `source` is always one of: `"clickhouse" | "kite_ws" | "mock"`.

## 2) Market data: DepthSnapshot

### 2.1 DTO

```json
{
  "type": "depth_snapshot",
  "symbol": "NFO:49543",
  "instrument_id": 49543,
  "capture_time_ms": 1767521472123,

  "bids": [
    {"price": 6.7, "size": 225, "orders": 1},
    {"price": 6.6, "size": 225, "orders": 2}
  ],
  "asks": [
    {"price": 6.8, "size": 975, "orders": 3},
    {"price": 6.85, "size": 300, "orders": 1}
  ],

  "trade": {
    "last_price": 6.75,
    "last_qty": 150,
    "total_traded_quantity": 131775,
    "average_traded_price": 7.17,
    "total_buy_quantity": 118425,
    "total_sell_quantity": 58425,

    "oi": null,
    "oi_day_high": null,
    "oi_day_low": null,

    "last_trade_time_s": 1746772551,
    "exchange_timestamp_s": 1746772551,

    "ohlc": {"open": 7.65, "high": 8.4, "low": 6.2, "close": 8.15}
  },

  "source": "clickhouse",
  "is_partial": false,
  "is_stale": false
}
```

### 2.2 Invariants (enforced server-side)

- `bids.length >= 1` and `asks.length >= 1`
- bids strictly descending by `price`
- asks strictly ascending by `price`
- `bids[0].price < asks[0].price`

If invalid:

- drop the snapshot from downstream feature computation
- emit an explicit error/metric/log

## 3) Features: FeatureSnapshot (minimal wire contract)

The internal feature set can evolve, but the DTO should remain stable and explicit.

### 3.1 DTO

```json
{
  "type": "feature_snapshot",
  "symbol": "NFO:49543",
  "instrument_id": 49543,
  "capture_time_ms": 1767521472123,

  "features": {
    "spread": 0.10,
    "mid": 6.75,
    "microprice": 6.74,

    "imbalance_top1": 0.23,
    "imbalance_top5": 0.12,

    "depth_slope": -0.08,
    "lpi": 1.42,

    "ofi_approx": -75,
    "liquidity_shock": false
  },

  "quality": {
    "source": "clickhouse",
    "is_partial": false,
    "is_stale": false
  }
}
```

Rules:

- if `quality.is_stale=true` ⇒ server should suppress fragile values (e.g., `microprice=null`)
- if features cannot be computed safely ⇒ omit them or set them to `null`, and keep quality flags truthful

## 4) Execution hint: ExecutionHint

### 4.1 DTO

```json
{
  "type": "execution_hint",
  "symbol": "NFO:49543",
  "instrument_id": 49543,
  "capture_time_ms": 1767521472123,

  "posture": "WAIT",
  "reasons": ["FEED_STALE"],

  "quality": {
    "source": "clickhouse",
    "is_partial": false,
    "is_stale": true
  }
}
```

Allowed postures:

- `PASSIVE`
- `AGGRESSIVE`
- `WAIT` (always valid; safe default)

## 5) Payoff platform (high-level wire contract)

The payoff API is specified in `docs/payoff/payoff_feature_spec.md`.

Minimum response additions for auditability/determinism:

- `assumptions`: model + parameters (rate, dividend, vol inputs, day-count)
- `engine_version`: semantic version or git sha
- `determinism`: seed + flags (seed can be 0 for non-stochastic computations)
