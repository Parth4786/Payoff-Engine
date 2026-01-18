# Payoff Engine - API Reference

> **Purpose**: Complete REST API endpoint specifications for frontend integration.

## Base URL

```
Development: http://localhost:8080
Production:  https://api.payoff-engine.com
```

## Common Headers

```http
Content-Type: application/json
Accept: application/json
```

## Response Format

All responses follow this structure:

**Success:**
```json
{
  "status": "ok",
  "data": { ... },
  "timestamp": 1767521472123
}
```

**Error:**
```json
{
  "error": "Error message",
  "code": "ERROR_CODE",
  "details": { ... }
}
```

---

## 📍 System Endpoints

### GET /api/health

Health check endpoint.

**Response:**
```json
{
  "status": "ok",
  "timestamp": "1767521472123",
  "version": "1.0.0"
}
```

---

### GET /api/config

Get current configuration (for debugging).

**Response:**
```json
{
  "clickhouse": {
    "host": "localhost",
    "port": 8123,
    "database": "tick_data_db",
    "connected": true
  },
  "kite": {
    "api_key": "***",
    "authenticated": true
  },
  "live_mode": false
}
```

---

## 📈 Payoff Endpoints

### POST /api/payoff/calculate

Calculate payoff curve for a strategy.

**Request Body:**
```json
{
  "underlying": "NIFTY",
  "spot": 26300,
  "iv": 0.15,
  "legs": [
    {
      "type": "CE",
      "side": "BUY",
      "strike": 26300,
      "qty": 1,
      "lot": 25,
      "premium": 250
    },
    {
      "type": "CE",
      "side": "SELL",
      "strike": 26500,
      "qty": 1,
      "lot": 25,
      "premium": 150
    }
  ],
  "scenario": {
    "spot_shift_pct": 0,
    "iv_shift_pct": 0,
    "days_forward": 0
  }
}
```

**Response:**
```json
{
  "strategy": "Bull Call Spread",
  "underlying": "NIFTY",
  "spot": 26300,
  "max_profit": 5000,
  "max_loss": -2500,
  "net_premium": -2500,
  "breakevens": [26400],
  "points": [
    {"spot": 25000, "pnl": -2500},
    {"spot": 26300, "pnl": -2500},
    {"spot": 26400, "pnl": 0},
    {"spot": 26500, "pnl": 5000},
    {"spot": 27000, "pnl": 5000}
  ],
  "greeks": {
    "delta": 0.48,
    "gamma": 0.0008,
    "theta": -25.50,
    "vega": 85.00,
    "rho": 12.00
  }
}
```

---

### POST /api/greeks/calculate

Calculate Greeks for a single option.

**Request Body:**
```json
{
  "spot": 26300,
  "strike": 26300,
  "iv": 0.15,
  "dte": 26,
  "type": "CE"
}
```

**Response:**
```json
{
  "spot": 26300,
  "strike": 26300,
  "iv": 0.15,
  "dte": 26,
  "type": "CE",
  "price": 285.50,
  "greeks": {
    "delta": 0.5234,
    "gamma": 0.0012,
    "theta": -15.25,
    "vega": 42.50,
    "rho": 8.75
  }
}
```

---

### GET /api/chain/greeks

Get Greeks for entire option chain.

**Query Parameters:**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `spot` | number | 26300 | Underlying price |
| `iv` | number | 0.15 | Implied volatility |
| `dte` | number | 26 | Days to expiry |
| `step` | number | 50 | Strike step size |
| `strikes` | number | 21 | Number of strikes |

**Example:**
```
GET /api/chain/greeks?spot=26300&iv=0.15&dte=26&step=100&strikes=11
```

**Response:**
```json
{
  "spot": 26300,
  "iv": 0.15,
  "dte": 26,
  "chain": [
    {
      "strike": 25800,
      "call": {
        "price": 520.00,
        "delta": 0.72,
        "gamma": 0.0008,
        "theta": -12.00,
        "vega": 35.00
      },
      "put": {
        "price": 18.50,
        "delta": -0.28,
        "gamma": 0.0008,
        "theta": -8.00,
        "vega": 35.00
      }
    },
    {
      "strike": 25900,
      "call": { ... },
      "put": { ... }
    }
  ]
}
```

---

### POST /api/sensitivity

Generate sensitivity surface (2D heatmap).

**Request Body:**
```json
{
  "spot": 26300,
  "strike": 26300,
  "iv": 0.15,
  "type": "delta"
}
```

**Supported surface types:**
- `delta` - Delta vs Spot × Time
- `gamma` - Gamma vs Spot × Time
- `theta` - Theta vs Spot × Time
- `vega` - Vega vs Spot × Time
- `pnl` - P&L vs Spot × Time

**Response:**
```json
{
  "type": "delta",
  "spot_center": 26300,
  "strike": 26300,
  "iv": 0.15,
  "surface": [
    {
      "spot": 23670,
      "values": [
        {"days": 5, "value": 0.12},
        {"days": 15, "value": 0.18},
        {"days": 25, "value": 0.25},
        {"days": 35, "value": 0.32},
        {"days": 45, "value": 0.38},
        {"days": 55, "value": 0.42}
      ]
    },
    {
      "spot": 24460,
      "values": [ ... ]
    }
  ]
}
```

---

### POST /api/iv/calculate

Calculate implied volatility from option price.

**Request Body:**
```json
{
  "spot": 26300,
  "strike": 26300,
  "price": 285.50,
  "dte": 26,
  "type": "CE"
}
```

**Response:**
```json
{
  "spot": 26300,
  "strike": 26300,
  "price": 285.50,
  "dte": 26,
  "type": "CE",
  "iv": 0.1523,
  "iv_pct": 15.23
}
```

**Error Response (if IV calculation fails):**
```json
{
  "spot": 26300,
  "strike": 26300,
  "price": 5.00,
  "dte": 26,
  "type": "CE",
  "iv": null,
  "error": "IV calculation failed to converge"
}
```

---

## 📊 Market Data Endpoints

### GET /api/quote

Get current quote for a symbol.

**Query Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `symbol` | string | Symbol to query (e.g., "NIFTY") |

**Response:**
```json
{
  "symbol": "NIFTY",
  "ltp": 26300.00,
  "change": 125.50,
  "change_pct": 0.48,
  "volume": 15000000,
  "oi": 12500000,
  "bid": 26299.50,
  "ask": 26300.50,
  "timestamp": "1767521472123"
}
```

---

## 🔍 Screener Endpoints

### GET /api/screener/market

**Main screener endpoint** - Get market state at a specific timestamp.

**Query Parameters:**
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `timestamp` | number | **required** | Unix ms timestamp |
| `exchanges` | string | all | Comma-separated (NFO,NSE,BFO) |
| `underlyings` | string | all | Comma-separated (NIFTY,BANKNIFTY) |
| `include_options` | bool | true | Include options |
| `include_futures` | bool | true | Include futures |
| `include_equities` | bool | false | Include equities |
| `include_calls` | bool | true | Include calls |
| `include_puts` | bool | true | Include puts |
| `min_dte` | number | - | Minimum days to expiry |
| `max_dte` | number | - | Maximum days to expiry |
| `only_itm` | bool | false | Only ITM options |
| `only_atm` | bool | false | Only ATM options |
| `only_otm` | bool | false | Only OTM options |
| `min_volume` | number | - | Minimum volume |
| `min_oi` | number | - | Minimum open interest |
| `min_iv` | number | - | Minimum IV (decimal) |
| `max_iv` | number | - | Maximum IV (decimal) |
| `max_spread_pct` | number | - | Maximum spread % |
| `sort_by` | string | volume | Sort field |
| `sort_order` | string | desc | asc or desc |
| `offset` | number | 0 | Pagination offset |
| `limit` | number | 100 | Max results |

**Example:**
```
GET /api/screener/market?timestamp=1767521472123&underlyings=NIFTY&min_dte=7&max_dte=30&only_atm=true&limit=50
```

**Response:**
```json
{
  "timestamp": 1767521472123,
  "underlyings": [
    {
      "symbol": "NIFTY",
      "spot_price": 26300,
      "prev_close": 26150,
      "change": 150,
      "change_pct": 0.57,
      "atm_iv": 0.142,
      "pcr_oi": 1.25
    }
  ],
  "instruments": [
    {
      "instrument_id": 49543,
      "tradingsymbol": "NIFTY25JAN26300CE",
      "underlying": "NIFTY",
      "exchange": "NFO",
      "instrument_type": 3,
      "strike": 26300,
      "option_type": "CE",
      "expiry_ms": 1767139200000,
      "days_to_expiry": 12,
      "last_price": 285.50,
      "bid_price": 284.00,
      "ask_price": 287.00,
      "mid_price": 285.50,
      "spread": 3.00,
      "spread_pct": 1.05,
      "volume": 5250000,
      "open_interest": 12500000,
      "oi_change": 250000,
      "iv": 0.1523,
      "iv_pct": 15.23,
      "iv_percentile": 35,
      "delta": 0.52,
      "gamma": 0.0012,
      "theta": -15.25,
      "vega": 42.50,
      "moneyness": 0,
      "is_itm": false,
      "is_atm": true,
      "is_otm": false,
      "timestamp": 1767521472123
    }
  ],
  "total_instruments": 1,
  "options_count": 1,
  "futures_count": 0,
  "equities_count": 0,
  "query_time_ms": 45.2,
  "calc_time_ms": 123.5
}
```

---

### GET /api/screener/timestamps

Get available timestamps in a range.

**Query Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `start` | number | Start timestamp (Unix ms) |
| `end` | number | End timestamp (Unix ms) |
| `interval` | number | Sample interval in seconds (default: 60) |

**Response:**
```json
{
  "timestamps": [
    1767521400000,
    1767521460000,
    1767521520000
  ],
  "count": 3
}
```

---

### GET /api/screener/underlyings

Get available underlyings.

**Response:**
```json
{
  "underlyings": [
    "NIFTY",
    "BANKNIFTY",
    "FINNIFTY",
    "MIDCPNIFTY"
  ]
}
```

---

### GET /api/screener/expiries

Get available expiries for an underlying.

**Query Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `underlying` | string | Underlying symbol |
| `as_of` | number | As of timestamp (Unix ms) |

**Response:**
```json
{
  "underlying": "NIFTY",
  "expiries": [
    {"expiry_ms": 1767139200000, "label": "30 Jan 2026", "dte": 12},
    {"expiry_ms": 1767744000000, "label": "06 Feb 2026", "dte": 19},
    {"expiry_ms": 1768348800000, "label": "13 Feb 2026", "dte": 26}
  ]
}
```

---

### GET /api/screener/chain

Get option chain for underlying.

**Query Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `underlying` | string | Underlying symbol |
| `expiry_ms` | number | Expiry timestamp |
| `timestamp` | number | As of timestamp |

**Response:**
```json
{
  "underlying": "NIFTY",
  "spot_price": 26300,
  "expiry_ms": 1767139200000,
  "timestamp": 1767521472123,
  "atm_strike": 26300,
  "max_pain": 26250,
  "chain": [
    {
      "strike": 26200,
      "call": {
        "instrument_id": 49540,
        "last_price": 385.00,
        "iv": 0.148,
        "delta": 0.62,
        "volume": 1250000,
        "open_interest": 5000000
      },
      "put": {
        "instrument_id": 49541,
        "last_price": 95.00,
        "iv": 0.152,
        "delta": -0.38,
        "volume": 800000,
        "open_interest": 4500000
      },
      "net_oi": 500000,
      "net_volume": 450000
    }
  ]
}
```

---

### GET /api/screener/iv-surface

Get IV surface data for 3D visualization.

**Query Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `underlying` | string | Underlying symbol |
| `timestamp` | number | As of timestamp |

**Response:**
```json
{
  "underlying": "NIFTY",
  "spot_price": 26300,
  "timestamp": 1767521472123,
  "surface": [
    {"strike": 25800, "dte": 12, "iv": 0.168},
    {"strike": 25800, "dte": 19, "iv": 0.162},
    {"strike": 25800, "dte": 26, "iv": 0.158},
    {"strike": 26000, "dte": 12, "iv": 0.155},
    {"strike": 26000, "dte": 19, "iv": 0.152}
  ]
}
```

---

### POST /api/screener/replay

Replay instruments over time range (for drill-down analysis).

**Request Body:**
```json
{
  "instrument_ids": [49543, 49545],
  "start": 1767518400000,
  "end": 1767521472123,
  "interval_ms": 1000,
  "include_greeks": true,
  "include_depth": false
}
```

**Response:**
```json
{
  "request": {
    "instrument_ids": [49543, 49545],
    "start": 1767518400000,
    "end": 1767521472123
  },
  "snapshots": [
    {
      "timestamp": 1767518400000,
      "instrument_id": 49543,
      "tradingsymbol": "NIFTY25JAN26300CE",
      "last_price": 275.00,
      "bid_price": 274.00,
      "ask_price": 276.00,
      "volume": 4800000,
      "oi": 12000000,
      "iv": 0.1485,
      "delta": 0.51,
      "price_change": 0,
      "price_change_pct": 0
    },
    {
      "timestamp": 1767518401000,
      "instrument_id": 49543,
      "last_price": 276.50,
      "price_change": 1.50,
      "price_change_pct": 0.55
    }
  ],
  "summary": {
    "start_price": 275.00,
    "end_price": 285.50,
    "total_change": 10.50,
    "total_change_pct": 3.82,
    "max_price": 290.00,
    "min_price": 272.00,
    "total_ticks": 3072,
    "duration_ms": 3072000
  }
}
```

---

## 🔴 Error Codes

| Code | HTTP Status | Description |
|------|-------------|-------------|
| `MISSING_PARAM` | 400 | Required parameter missing |
| `INVALID_PARAM` | 400 | Invalid parameter value |
| `NOT_FOUND` | 404 | Resource not found |
| `DATA_UNAVAILABLE` | 503 | Data source unavailable |
| `CALC_FAILED` | 500 | Calculation failed |

---

## 📡 WebSocket API

See [WEBSOCKET_PROTOCOL.md](./WEBSOCKET_PROTOCOL.md) for real-time streaming API.

---

## 🔗 Related Documents

- [DATA_SCHEMA.md](./DATA_SCHEMA.md) - Data type definitions
- [DATA_FLOW.md](./DATA_FLOW.md) - Data pipeline flows
- [FRONTEND_INTEGRATION.md](./FRONTEND_INTEGRATION.md) - Integration guide
