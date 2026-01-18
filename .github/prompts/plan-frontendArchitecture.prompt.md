# Production-Grade Options Analytics Frontend

Build a desk-grade, risk-first options strategy and analytics console that surpasses Sensibull/Opstra with curvature visualization, kill-zone warnings, and real-time WebSocket integration.

**PRIMARY FOCUS: Historical Replay with No-Lookahead Simulation**

The core differentiator is **deterministic replay mode** where:
1. At timestamp T, you only see data available up to T (no future data)
2. You see predicted payoff/Greeks at T
3. As time advances to T+N, actual data becomes visible
4. Compare predicted vs actual to find insights and improve decision-making

---

## Tech Stack Decision: Next.js 14 + React 18

### Why This Stack

| Choice | Rationale |
|--------|-----------|
| **Next.js 14** | Industry standard, great DX, easy deployment, SSR for fast initial load |
| **React 18** | Concurrent rendering = smooth updates during heavy data streams |
| **TypeScript** | Strict mode for type safety with backend DTOs |
| **Zustand** | Lightweight state management (not Redux bloat), perfect for real-time data |
| **TanStack Query** | Smart caching, request deduplication, background refetch |
| **Tailwind CSS + shadcn/ui** | Fast development, consistent design system, dark theme ready |
| **Recharts** | Simple, declarative payoff/Greeks charts |
| **Custom Canvas/WebGL** | Heatmaps and sensitivity maps (DOM can't handle pixel-level updates) |
| **TanStack Virtual** | Virtualized tables for 10,000+ row screener without lag |

### Load & Scalability: Where Concerns Actually Live

| Concern | Where It Lives | Solution |
|---------|----------------|----------|
| Multiple clients hitting APIs | **Backend** (C++) | ✅ Already handles multi-client |
| Real-time data broadcast | **Backend** WebSocket | ✅ C++ broadcasts to N clients |
| Heavy chart rendering | **Each browser** (local) | React 18 concurrent mode |
| Strategy/Greeks calculations | **Backend** only | ✅ Frontend never calculates |
| Large option chain tables | **Each browser** | TanStack Virtual (renders ~50 visible rows) |
| Sensitivity heatmaps | **Each browser** | Canvas/WebGL (60fps pixel updates) |

### Performance Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  USER'S BROWSER                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  React 18 Concurrent Mode                            │   │
│  │  - Prioritizes user interactions over data updates   │   │
│  │  - Batches state updates automatically               │   │
│  │  - Won't freeze during WebSocket data floods         │   │
│  └─────────────────────────────────────────────────────┘   │
│                          │                                  │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  WebSocket Manager (throttled)                       │   │
│  │  - Buffers incoming messages                         │   │
│  │  - Updates state at 500ms intervals max              │   │
│  │  - Drops stale data intelligently                    │   │
│  └─────────────────────────────────────────────────────┘   │
│                          │                                  │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Virtualized Tables (TanStack Virtual)               │   │
│  │  - 10,000 rows? Only renders ~50 visible             │   │
│  │  - Smooth scrolling, no memory bloat                 │   │
│  └─────────────────────────────────────────────────────┘   │
│                          │                                  │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Canvas Heatmaps (not DOM)                           │   │
│  │  - Direct pixel manipulation                         │   │
│  │  - 60fps even with 100x100 grid updates              │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│  C++ BACKEND (handles real multi-client load)               │
│  - WebSocket broadcast to N clients simultaneously          │
│  - All payoff/Greeks/IV calculations                        │
│  - ClickHouse queries for historical data                   │
│  - Lock-free shared market data cache                       │
└─────────────────────────────────────────────────────────────┘
```

---

## Backend Capability Analysis

### ✅ Fully Supported Features (Backend Ready)

| Feature | Endpoint | Notes |
|---------|----------|-------|
| Payoff calculation | `POST /api/payoff/calculate` | Multi-leg, scenarios, Greeks included |
| Greeks (single option) | `POST /api/greeks/calculate` | Delta, Gamma, Theta, Vega, Rho |
| Greeks (full chain) | `GET /api/chain/greeks` | Configurable strikes/step |
| IV calculation | `POST /api/iv/calculate` | From option price |
| Sensitivity surfaces | `POST /api/sensitivity` | Delta, Gamma, Theta, Vega, PnL vs Spot×Time |
| Market screener | `GET /api/screener/market` | Full filtering, sorting, pagination |
| Option chain | `GET /api/screener/chain` | Max pain, ATM strike, net OI |
| IV surface (3D) | `GET /api/screener/iv-surface` | Strike × DTE × IV |
| Available timestamps | `GET /api/screener/timestamps` | For historical replay |
| Available underlyings | `GET /api/screener/underlyings` | NIFTY, BANKNIFTY, etc. |
| Available expiries | `GET /api/screener/expiries` | DTE labels |
| Historical replay | `POST /api/screener/replay` | Time-range instrument replay |
| Live quotes | `GET /api/quote` | LTP, bid/ask, volume, OI |
| WebSocket depth | `depth_snapshot` | 5-level bid/ask |
| WebSocket features | `feature_snapshot` | Microprice, imbalance, OFI |
| WebSocket strategy | `strategy_update` | Live P&L updates |
| Execution hints | `execution_hint` | PASSIVE/AGGRESSIVE/WAIT posture |
| Replay engine | `PayoffReplayEngine` (C++) | Strategy backtesting with payoff snapshots |

### ⚠️ Planned but Need Frontend-Side Logic

| Feature | Status | Solution |
|---------|--------|----------|
| Kill-zone detection | Backend provides sensitivity surface | Frontend overlays threshold coloring |
| Margin estimation | ❌ Not in backend | Either add endpoint or use heuristic |
| Strategy naming | Backend returns `"strategy": "Bull Call Spread"` | ✅ Automatic |

### 🚀 Extra Features Backend Supports (Add to UI)

| Feature | Endpoint | UI Enhancement |
|---------|----------|----------------|
| **Max Pain** | `/api/screener/chain` returns `max_pain` | Show on option chain view |
| **IV Percentile** | Screener returns `iv_percentile` | Color-code high/low IV instruments |
| **OI Change** | Screener returns `oi_change` | Show accumulation/distribution |
| **Spread %** | Screener returns `spread_pct` | Filter illiquid options |
| **PCR (Put-Call Ratio)** | Screener returns `pcr_oi` | Show sentiment indicator |
| **Execution Hints** | WebSocket `execution_hint` | Real-time trading posture |
| **Market Microstructure** | `feature_snapshot` | Microprice, imbalance, LPI |

---

## 🔴 NEW BACKEND ENDPOINTS NEEDED FOR REPLAY MODE

These endpoints are **required** to enable the prediction-vs-reality comparison feature:

### 1. Strategy Replay with Payoff Snapshots

```
POST /api/replay/strategy
```

**Purpose:** Run strategy through historical data, get payoff at each timestamp

**Request:**
```json
{
  "strategy": {
    "underlying": "NIFTY",
    "legs": [
      {"type": "CE", "side": "BUY", "strike": 26300, "qty": 1, "lot": 25, "premium": 250}
    ]
  },
  "start_timestamp": 1767518400000,
  "end_timestamp": 1767604800000,
  "interval_ms": 60000,
  "include_greeks": true
}
```

**Response:**
```json
{
  "snapshots": [
    {
      "timestamp": 1767518400000,
      "underlying_price": 26280,
      "total_pnl": -2500,
      "greeks": {"delta": 0.52, "gamma": 0.0012, "theta": -15.25, "vega": 42.50},
      "leg_prices": [{"strike": 26300, "price": 250, "iv": 0.15}]
    },
    {
      "timestamp": 1767518460000,
      "underlying_price": 26320,
      "total_pnl": -1800,
      "greeks": {"delta": 0.55, "gamma": 0.0011, "theta": -14.80, "vega": 41.20}
    }
  ],
  "summary": {
    "initial_pnl": -2500,
    "final_pnl": +3200,
    "max_pnl": +5000,
    "min_pnl": -3100,
    "pnl_std_dev": 1250
  }
}
```

### 2. Prediction Snapshot at Historical Time

```
POST /api/replay/prediction
```

**Purpose:** Get what the predicted payoff LOOKED LIKE at time T (using only data available at T)

**Request:**
```json
{
  "strategy": {...},
  "as_of_timestamp": 1767518400000,
  "prediction_horizons": [
    {"days_forward": 1},
    {"days_forward": 3},
    {"days_forward": 7, "iv_shift_pct": -2}
  ]
}
```

**Response:**
```json
{
  "as_of_timestamp": 1767518400000,
  "market_state_at_time": {
    "underlying_price": 26280,
    "atm_iv": 0.152,
    "days_to_expiry": 12
  },
  "predictions": [
    {
      "horizon": {"days_forward": 1},
      "predicted_payoff": [
        {"spot": 26000, "pnl": -2500},
        {"spot": 26300, "pnl": -1200},
        {"spot": 26500, "pnl": +2800}
      ],
      "predicted_greeks": {"delta": 0.48, "gamma": 0.0015}
    }
  ]
}
```

### 3. Prediction vs Reality Comparison

```
POST /api/replay/compare
```

**Purpose:** Compare what was predicted at T with what actually happened at T+N

**Request:**
```json
{
  "strategy": {...},
  "prediction_timestamp": 1767518400000,
  "actual_timestamp": 1767604800000,
  "comparison_points": [26000, 26200, 26300, 26400, 26500]
}
```

**Response:**
```json
{
  "prediction_timestamp": 1767518400000,
  "actual_timestamp": 1767604800000,
  "time_elapsed_hours": 24,
  
  "at_prediction_time": {
    "underlying_price": 26280,
    "predicted_pnl_at_current_spot": -2500,
    "predicted_breakeven": 26400
  },
  
  "at_actual_time": {
    "underlying_price": 26450,
    "actual_pnl": +3800,
    "actual_breakeven": 26380
  },
  
  "deviation": {
    "pnl_deviation": +1300,
    "pnl_deviation_pct": 52,
    "breakeven_shift": -20,
    "iv_change": -0.018,
    "delta_drift": +0.08,
    "prediction_accuracy_score": 0.78
  },
  
  "insights": [
    "Actual profit exceeded prediction by ₹1,300 (+52%)",
    "IV dropped 1.8%, reducing Vega P&L contribution",
    "Spot moved +170, Delta gains dominated"
  ]
}
```

### 4. Replay Session Management

```
POST /api/replay/session/create
GET  /api/replay/session/{id}/state
POST /api/replay/session/{id}/step
POST /api/replay/session/{id}/seek
DELETE /api/replay/session/{id}
```

**Purpose:** Persistent replay session that can be paused, stepped, seeked

**Create Session:**
```json
{
  "strategy": {...},
  "start_timestamp": 1767518400000,
  "end_timestamp": 1767604800000,
  "speed": 1.0,
  "auto_calculate_payoff": true,
  "payoff_interval_ms": 60000
}
```

**Step Response:**
```json
{
  "session_id": "replay-abc123",
  "current_timestamp": 1767518460000,
  "progress_pct": 0.02,
  "market_state": {
    "underlying_price": 26290,
    "quotes": {...}
  },
  "payoff_snapshot": {
    "total_pnl": -2300,
    "greeks": {...}
  },
  "has_more": true
}
```

### 5. Historical Event Markers

```
GET /api/replay/events
```

**Purpose:** Get significant market events in time range for annotation

**Request:**
```
GET /api/replay/events?start=1767518400000&end=1767604800000&underlying=NIFTY
```

**Response:**
```json
{
  "events": [
    {
      "timestamp": 1767520000000,
      "type": "IV_SPIKE",
      "description": "ATM IV jumped 3% in 5 minutes",
      "magnitude": 0.03
    },
    {
      "timestamp": 1767525600000,
      "type": "PRICE_GAP",
      "description": "NIFTY gapped down 0.8% at open",
      "magnitude": -0.008
    },
    {
      "timestamp": 1767560000000,
      "type": "OI_BUILDUP",
      "description": "26300 CE saw 500K OI addition",
      "strike": 26300,
      "option_type": "CE"
    }
  ]
}
```

### 6. Batch Historical Payoff Points

```
POST /api/payoff/historical-batch
```

**Purpose:** Calculate payoff at multiple historical timestamps efficiently

**Request:**
```json
{
  "strategy": {...},
  "timestamps": [1767518400000, 1767521600000, 1767524800000],
  "include_greeks": true
}
```

**Response:**
```json
{
  "results": [
    {"timestamp": 1767518400000, "pnl": -2500, "greeks": {...}},
    {"timestamp": 1767521600000, "pnl": -1200, "greeks": {...}},
    {"timestamp": 1767524800000, "pnl": +800, "greeks": {...}}
  ]
}
```

---

## Steps

1. **Scaffold Next.js 14+ project** with TypeScript strict mode, Tailwind CSS, shadcn/ui, Zustand for state, and TanStack Query for data fetching

2. **Implement core data layer** with TypeScript interfaces matching backend DTOs — create `OptionLeg`, `Greeks`, `PayoffPoint`, `InstrumentSnapshot`, `DepthSnapshot` types

3. **Build Strategy Builder module** with drag-and-drop leg table, option chain integration, live risk warnings

4. **Create Payoff Visualization suite** including multi-layer payoff chart, animated time decay, breakeven markers

5. **Develop Sensitivity Curvature Maps** with Delta/Gamma/Vega/Theta heatmaps, kill-zone highlighting

6. **Implement Screener & Option Chain views** with virtualized tables, IV surface 3D, drag-to-strategy

7. **BUILD REPLAY MODE** (PRIMARY FOCUS) with timeline scrubber, prediction-vs-reality overlay, deviation tracking

---

## ASCII Wireframes by Page

### Page 1: Strategy Builder

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  ⚡ PAYOFF ENGINE          [Strategy ▾] [Analyze ▾] [Screener] [Risk] [Live]   │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  ┌─────────────────────────────────┐  ┌─────────────────────────────────────┐  │
│  │  📊 STRATEGY LEGS               │  │  ⛓️ OPTION CHAIN - NIFTY            │  │
│  │                                 │  │                                     │  │
│  │  [+ Add Leg]                    │  │  Expiry: [30 Jan ▾]  Spot: 26,300   │  │
│  │  ┌─────────────────────────┐   │  │                                     │  │
│  │  │ ● BUY  CE 26300 x1 ₹250 │   │  │  Strike │  CE    │  Δ   │  PE   │ Δ  │  │
│  │  │   [Edit] [×]            │   │  │  ───────┼────────┼──────┼───────┼────│  │
│  │  └─────────────────────────┘   │  │  26100  │ ₹425   │ .68  │ ₹58   │-.32│  │
│  │  ┌─────────────────────────┐   │  │  26200  │ ₹340   │ .61  │ ₹78   │-.39│  │
│  │  │ ○ SELL CE 26500 x1 ₹150 │   │  │  26300* │ ₹285   │ .52  │ ₹110  │-.48│  │
│  │  │   [Edit] [×]            │   │  │  26400  │ ₹220   │ .44  │ ₹155  │-.56│  │
│  │  └─────────────────────────┘   │  │  26500  │ ₹165   │ .36  │ ₹210  │-.64│  │
│  │                                 │  │  26600  │ ₹118   │ .28  │ ₹275  │-.72│  │
│  │  ─────────────────────────────  │  │                                     │  │
│  │                                 │  │  [Drag strike to add leg →]         │  │
│  │  Net Premium:    -₹2,500        │  └─────────────────────────────────────┘  │
│  │  Max Profit:     +₹5,000        │                                           │
│  │  Max Loss:       -₹2,500        │  ┌─────────────────────────────────────┐  │
│  │  Breakeven:       26,400        │  │  ⚠️ RISK WARNINGS                   │  │
│  │                                 │  │                                     │  │
│  │  Strategy: Bull Call Spread     │  │  ✅ Limited loss strategy           │  │
│  │                                 │  │  ⚠️ Gamma exposure high < T-3       │  │
│  │  [Calculate Payoff →]           │  │  ℹ️ Max profit at ₹26,500+          │  │
│  │                                 │  │                                     │  │
│  └─────────────────────────────────┘  └─────────────────────────────────────┘  │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  STRATEGY GREEKS (Aggregate)                                              │  │
│  │                                                                           │  │
│  │   Δ Delta    Γ Gamma     Θ Theta      V Vega       ρ Rho                 │  │
│  │   +0.16      +0.0004     -₹10.25      +₹42.50      +₹3.25                │  │
│  │   ████░░     ██░░░░      ████████     ██████░░     ██░░░░                │  │
│  │                                                                           │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

**Components:**
- `LegTable` — Add/edit/remove legs with validation
- `OptionChainPicker` — Draggable strikes, shows IV/Delta
- `RiskWarnings` — Kill-zone alerts, unlimited loss warning
- `StrategySummary` — Credit/debit, max P/L, breakevens
- `GreeksDisplay` — Aggregate Delta/Gamma/Theta/Vega/Rho

---

### Page 2: Payoff Analysis

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  ⚡ PAYOFF ENGINE          [Strategy] [Analyze ▾] [Screener] [Risk] [Live]     │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │  [Expiry] [T+1] [T+3] [T+5] [IV-2%] [IV+2%]         Bull Call Spread    │   │
│  ├─────────────────────────────────────────────────────────────────────────┤   │
│  │                                                                         │   │
│  │  P&L                                                                    │   │
│  │   ▲                                                                     │   │
│  │   │                              ┌─────────────────────                 │   │
│  │ +5k│                            ╱│                    Max: +₹5,000      │   │
│  │   │                           ╱  │                                      │   │
│  │   │                         ╱    │                                      │   │
│  │ +2k│                      ╱      │     ─── Expiry                       │   │
│  │   │                     ╱        │     ─── T+5                          │   │
│  │   ├─────────────────────────────────────────────────────────────────    │   │
│  │  0│                   ╱│ BE: 26,400                                     │   │
│  │   │                 ╱  │                                                │   │
│  │   │               ╱    │                                                │   │
│  │-2k│─────────────╱      │                                                │   │
│  │   │             │      │                    Max Loss: -₹2,500           │   │
│  │   └─────────────┴──────┴──────────────────────────────────────────►     │   │
│  │       25,500   26,000   26,400   26,500   27,000   27,500   Spot        │   │
│  │                         ▲                                               │   │
│  │                    Current: 26,300                                      │   │
│  │                                                                         │   │
│  └─────────────────────────────────────────────────────────────────────────┘   │
│                                                                                 │
│  ┌────────────────────────────────────┐  ┌──────────────────────────────────┐  │
│  │  📋 PAYOFF TABLE                   │  │  📊 KEY METRICS                  │  │
│  │                                    │  │                                  │  │
│  │  Spot    │ Expiry │ T+5  │ IV-2%  │  │  Breakeven(s):    26,400         │  │
│  │  ────────┼────────┼──────┼────────│  │  Max Profit:      +₹5,000        │  │
│  │  25,500  │ -2,500 │-2,100│ -2,200 │  │  Max Loss:        -₹2,500        │  │
│  │  26,000  │ -2,500 │-1,500│ -1,800 │  │  Risk/Reward:     1:2            │  │
│  │  26,300  │ -2,500 │  -800│ -1,200 │  │  Prob. Profit:    ~45%           │  │
│  │  26,400  │      0 │  +200│   -100 │  │  Net Premium:     -₹2,500        │  │
│  │  26,500  │ +5,000 │+3,800│ +4,200 │  │                                  │  │
│  │  27,000  │ +5,000 │+4,500│ +4,800 │  │  [🔴 = loss accelerating]        │  │
│  │                                    │  │                                  │  │
│  └────────────────────────────────────┘  └──────────────────────────────────┘  │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

**Components:**
- `PayoffChart` — Recharts line chart with multi-curve overlay
- `ScenarioTabs` — Toggle Expiry/T+N/IV scenarios
- `PayoffTable` — Spot vs P&L grid with color intensity
- `KeyMetrics` — Breakevens, max P/L, risk-reward

---

### Page 3: Sensitivity Maps

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  ⚡ PAYOFF ENGINE          [Strategy] [Analyze ▾] [Screener] [Risk] [Live]     │
├─────────────────────────────────────────────────────────────────────────────────┤
│  Sub-nav: [Payoff Curve] [Sensitivity Maps ●] [IV Surface]                     │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  ┌─────────────────────────────────────┐  ┌─────────────────────────────────┐  │
│  │  DELTA SURFACE (Spot × Time)        │  │  GAMMA SURFACE (Spot × Time)    │  │
│  │                                     │  │                                 │  │
│  │  Days↓   Spot →                     │  │  Days↓   Spot →                 │  │
│  │       25.5  26.0  26.3  26.5  27.0  │  │       25.5  26.0  26.3  26.5    │  │
│  │  T-30  🟦    🟦    🟩    🟨    🟨   │  │  T-30  🟦    🟦    🟨    🟦    │  │
│  │  T-20  🟦    🟦    🟩    🟨    🟧   │  │  T-20  🟦    🟨    🟧    🟨    │  │
│  │  T-10  🟦    🟩    🟨    🟧    🟧   │  │  T-10  🟨    🟧    🟥    🟧    │  │
│  │  T-5   🟦    🟩    🟧    🟥    🟥   │  │  T-5   🟧    🟥    🔴    🟥    │  │
│  │  T-1   🟩    🟨    🟥    🟥    🔴   │  │  T-1   🟥    🔴    ⬛    🔴    │  │
│  │                                     │  │                                 │  │
│  │  🟦 -0.2  🟩 0.0  🟨 +0.3  🟥 +0.6  │  │  🟦 low  🟨 med  🔴 HIGH       │  │
│  │         ▲ Current spot              │  │  ⬛ = KILL ZONE (gamma spike)   │  │
│  └─────────────────────────────────────┘  └─────────────────────────────────┘  │
│                                                                                 │
│  ┌─────────────────────────────────────┐  ┌─────────────────────────────────┐  │
│  │  THETA DECAY (Spot × Time)          │  │  VEGA EXPOSURE (Spot × IV)      │  │
│  │                                     │  │                                 │  │
│  │  Days↓   Spot →                     │  │  IV↓    Spot →                  │  │
│  │       25.5  26.0  26.3  26.5  27.0  │  │       25.5  26.0  26.3  26.5    │  │
│  │  T-30  🟩    🟩    🟩    🟩    🟩   │  │  10%   🟩    🟩    🟩    🟩    │  │
│  │  T-20  🟩    🟩    🟨    🟨    🟩   │  │  12%   🟩    🟨    🟨    🟩    │  │
│  │  T-10  🟨    🟨    🟧    🟧    🟨   │  │  15%*  🟨    🟧    🟧    🟨    │  │
│  │  T-5   🟧    🟧    🟥    🟥    🟧   │  │  18%   🟧    🟥    🟥    🟧    │  │
│  │  T-1   🟥    🟥    🔴    🔴    🟥   │  │  20%   🟥    🔴    🔴    🟥    │  │
│  │                                     │  │                                 │  │
│  │  🟩 low decay   🔴 rapid decay      │  │  * Current IV level             │  │
│  └─────────────────────────────────────┘  └─────────────────────────────────┘  │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  🔴 KILL ZONE SUMMARY                                                     │  │
│  │                                                                           │  │
│  │  ⚠️ Gamma explosion risk: Spot 26,200-26,400, T < 3 days                 │  │
│  │  ⚠️ Theta decay accelerates: T < 5 days near ATM                         │  │
│  │  ℹ️ Safe zone: Spot > 26,500 or T > 10 days                              │  │
│  │                                                                           │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

**Components:**
- `DeltaSurface` — Canvas heatmap, Spot×Time
- `GammaHeatmap` — Canvas with kill-zone overlay
- `ThetaSurface` — Time decay visualization
- `VegaSurface` — IV sensitivity
- `KillZoneOverlay` — Red zone + summary

**Backend:** `POST /api/sensitivity` returns `surface[]` with `{spot, values: [{days, value}]}`

---

### Page 4: Market Screener

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  ⚡ PAYOFF ENGINE          [Strategy] [Analyze] [Screener ●] [Risk] [Live]     │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  FILTERS                                                                  │  │
│  │                                                                           │  │
│  │  Underlying: [NIFTY ▾]   Expiry: [30 Jan ▾]   Timestamp: [Live ▾]        │  │
│  │                                                                           │  │
│  │  Type: [●CE ○PE ○Both]   Money: [○ITM ●ATM ○OTM ○All]                    │  │
│  │                                                                           │  │
│  │  Min Volume: [1M    ]   Min OI: [5M    ]   Max Spread: [2%   ]           │  │
│  │  IV Range:   [10%   ] to [25%   ]   DTE: [7    ] to [30   ]              │  │
│  │                                                                           │  │
│  │  [Apply Filters]  [Reset]                         Sort: [Volume ▾] [↓]   │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  UNDERLYING SUMMARY                                PCR: 1.25  ATM IV: 14%│  │
│  │  NIFTY  26,300 (+150, +0.57%)   Max Pain: 26,250                         │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  INSTRUMENTS (247 results)                                    [Export]   │  │
│  │                                                                           │  │
│  │  Symbol              │Strike│Type│ LTP  │  IV  │  Δ   │ Vol  │  OI  │Add │  │
│  │  ────────────────────┼──────┼────┼──────┼──────┼──────┼──────┼──────┼────│  │
│  │  NIFTY25JAN26300CE   │26300 │ CE │285.50│15.2% │ +.52 │5.25M │12.5M │[+] │  │
│  │  NIFTY25JAN26400CE   │26400 │ CE │220.00│14.8% │ +.44 │4.10M │ 9.8M │[+] │  │
│  │  NIFTY25JAN26200CE   │26200 │ CE │385.00│15.5% │ +.62 │3.85M │ 8.2M │[+] │  │
│  │  NIFTY25JAN26500CE   │26500 │ CE │165.00│14.5% │ +.36 │3.50M │ 7.5M │[+] │  │
│  │  NIFTY25JAN26100CE   │26100 │ CE │485.00│15.8% │ +.72 │2.90M │ 6.8M │[+] │  │
│  │  NIFTY25JAN26300PE   │26300 │ PE │110.00│15.0% │ -.48 │2.75M │ 6.2M │[+] │  │
│  │                                                                           │  │
│  │  ◀ 1 2 3 4 5 ... 12 ▶                                    Showing 1-20    │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  [+ Add to Strategy] adds selected row to Strategy Builder                     │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

**Components:**
- `FilterBar` — Underlying, expiry, moneyness, IV range, volume
- `UnderlyingSummary` — Spot, change, PCR, ATM IV, max pain
- `InstrumentTable` — Virtualized table, sortable columns
- `AddToStrategy` — Click [+] to add as leg

**Backend:** `GET /api/screener/market` with full filtering

---

### Page 5: Option Chain View

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  ⚡ PAYOFF ENGINE          [Strategy] [Analyze] [Screener ▾] [Risk] [Live]     │
├─────────────────────────────────────────────────────────────────────────────────┤
│  Sub-nav: [Screener] [Option Chain ●] [IV Surface 3D]                          │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  Underlying: [NIFTY ▾]   Expiry: [30 Jan ▾]   Spot: 26,300   Max Pain: 26,250  │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │                                                                           │  │
│  │            ◄─────────── CALLS ───────────►  │  ◄─────────── PUTS ────────►│  │
│  │                                                                           │  │
│  │   OI    │  Vol  │  LTP  │  IV  │  Δ  │Strike│  Δ   │  IV  │  LTP │  Vol  │  │
│  │  ───────┼───────┼───────┼──────┼─────┼──────┼──────┼──────┼──────┼───────│  │
│  │  6.8M   │ 2.9M  │ 485   │15.8% │ .72 │26100 │ -.28 │15.9% │  42  │ 1.2M  │  │
│  │  8.2M   │ 3.8M  │ 385   │15.5% │ .62 │26200 │ -.38 │15.6% │  78  │ 1.8M  │  │
│  │ ████████████████████████████████████│██████│██████████████████████████████│  │
│  │  12.5M  │ 5.2M  │ 285   │15.2% │ .52 │26300*│ -.48 │15.0% │ 110  │ 2.7M  │  │  ← ATM
│  │ ████████████████████████████████████│██████│██████████████████████████████│  │
│  │  9.8M   │ 4.1M  │ 220   │14.8% │ .44 │26400 │ -.56 │14.9% │ 155  │ 2.1M  │  │
│  │  7.5M   │ 3.5M  │ 165   │14.5% │ .36 │26500 │ -.64 │14.7% │ 210  │ 1.9M  │  │
│  │  5.2M   │ 2.2M  │ 118   │14.2% │ .28 │26600 │ -.72 │14.5% │ 275  │ 1.5M  │  │
│  │  3.8M   │ 1.5M  │  82   │13.9% │ .21 │26700 │ -.79 │14.3% │ 350  │ 1.1M  │  │
│  │                                                                           │  │
│  │  ███ = OI bar visualization                                               │  │
│  │                                                                           │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  ┌───────────────────────────┐  ┌────────────────────────────────────────────┐ │
│  │  OI ANALYSIS              │  │  QUICK ADD TO STRATEGY                     │ │
│  │                           │  │                                            │ │
│  │  Total Call OI: 85M       │  │  Click any cell to add leg:               │ │
│  │  Total Put OI:  68M       │  │                                            │ │
│  │  PCR (OI):      0.80      │  │  [Buy CE] [Sell CE] [Buy PE] [Sell PE]    │ │
│  │  Max Pain:      26,250    │  │                                            │ │
│  │                           │  │  Selected: 26300 CE                        │ │
│  └───────────────────────────┘  └────────────────────────────────────────────┘ │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

**Components:**
- `OptionChainGrid` — Calls left, Puts right, strike center
- `OIBars` — Visual OI representation
- `OIAnalysis` — PCR, max pain, total OI
- `QuickAdd` — Click cell → Buy/Sell → add to strategy

**Backend:** `GET /api/screener/chain` returns full chain with `max_pain`, `atm_strike`

---

### Page 6: IV Surface 3D

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  ⚡ PAYOFF ENGINE          [Strategy] [Analyze] [Screener ▾] [Risk] [Live]     │
├─────────────────────────────────────────────────────────────────────────────────┤
│  Sub-nav: [Screener] [Option Chain] [IV Surface 3D ●]                          │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │                           IV SURFACE                                      │  │
│  │                                                                           │  │
│  │              IV%                                                          │  │
│  │               ▲                                                           │  │
│  │           20%─│      ╱────╲                                               │  │
│  │               │     ╱      ╲                                              │  │
│  │           18%─│    ╱   ╱────╲╲                                            │  │
│  │               │   ╱   ╱      ╲╲                                           │  │
│  │           16%─│──╱───╱────────╲╲───────────────                           │  │
│  │               │ ╱   ╱          ╲╲      smile →                            │  │
│  │           14%─│╱───╱────────────╲╲──────────────                          │  │
│  │               │   ╱              ╲                                        │  │
│  │           12%─├──╱────────────────╲─────────────────                      │  │
│  │               │                   DTE →                                   │  │
│  │               └────────────────────────────────────► Strike              │  │
│  │                 25.5k  26k  26.3k  26.5k  27k                             │  │
│  │                              ▲                                            │  │
│  │                         Current spot                                      │  │
│  │                                                                           │  │
│  │   [Rotate] [Reset View] [2D Toggle]              DTE: 7 / 14 / 30 / 60   │  │
│  │                                                                           │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  SKEW ANALYSIS                                                            │  │
│  │                                                                           │  │
│  │  ATM IV:     14.2%         25Δ Put IV:   16.8%         Skew:  +2.6%       │  │
│  │  Term Slope: -0.8%/month   Smile Curvature: +1.2%                         │  │
│  │                                                                           │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

**Components:**
- `IVSurface3D` — Three.js / Plotly 3D surface
- `SkewAnalysis` — ATM IV, put skew, term structure

**Backend:** `GET /api/screener/iv-surface` returns `{strike, dte, iv}[]`

---

### Page 7: Risk Decomposition

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  ⚡ PAYOFF ENGINE          [Strategy] [Analyze] [Screener] [Risk ●] [Live]     │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  Strategy: Bull Call Spread          Current P&L: +₹1,250 (+50%)               │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  P&L ATTRIBUTION — Why did this strategy make money?                      │  │
│  │                                                                           │  │
│  │  Component     │ P&L Impact │ Bar                                        │  │
│  │  ──────────────┼────────────┼─────────────────────────────────────────── │  │
│  │  Delta P&L     │   +₹1,800  │ █████████████████████████████████ (+72%)   │  │
│  │  Gamma P&L     │     +₹350  │ ██████ (+14%)                              │  │
│  │  Theta Decay   │     -₹600  │ ████████████ (-24%)                        │  │
│  │  Vega P&L      │     -₹300  │ ██████ (-12%)                              │  │
│  │  ──────────────┼────────────┼────────────────────────────────────────────│  │
│  │  Net P&L       │   +₹1,250  │                                            │  │
│  │                                                                           │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  ┌─────────────────────────────────┐  ┌──────────────────────────────────────┐ │
│  │  GREEKS OVER TIME               │  │  INSIGHT                             │ │
│  │                                 │  │                                      │ │
│  │  Δ ─────╲                       │  │  ✅ Delta gains dominated (+₹1,800)  │ │
│  │          ╲                      │  │     Spot moved +150 in your favor    │ │
│  │           ╲───                  │  │                                      │ │
│  │  Γ ─────────────                │  │  ⚠️ Theta cost you ₹600              │ │
│  │                                 │  │     Consider rolling before T-5     │ │
│  │  Θ ──╲                          │  │                                      │ │
│  │       ╲                         │  │  ℹ️ Vega hurt due to IV crush       │ │
│  │        ╲───                     │  │     IV dropped from 16% to 14%       │ │
│  │                                 │  │                                      │ │
│  │  T-10   T-7   T-5   T-3   Now   │  │                                      │ │
│  └─────────────────────────────────┘  └──────────────────────────────────────┘ │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

**Components:**
- `PnLAttribution` — Breakdown by Delta/Gamma/Theta/Vega
- `GreeksTimeSeries` — Historical Greeks evolution
- `InsightPanel` — Plain English explanations

---

### Page 8: Live Monitor

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  ⚡ PAYOFF ENGINE          [Strategy] [Analyze] [Screener] [Risk] [Live ●]     │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  ACTIVE STRATEGIES                                                        │  │
│  │                                                                           │  │
│  │  Strategy          │ Underlying │  P&L   │ Δ Drift │ Status │ Actions    │  │
│  │  ──────────────────┼────────────┼────────┼─────────┼────────┼────────────│  │
│  │  Bull Call Spread  │ NIFTY      │ +1,250 │  -0.02  │   🟢   │ [×] [Edit] │  │
│  │  Iron Condor       │ BANKNIFTY  │   -450 │  +0.08  │   🟡   │ [×] [Edit] │  │
│  │  Straddle          │ NIFTY      │ +2,100 │  -0.15  │   🟢   │ [×] [Edit] │  │
│  │                                                                           │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  ┌─────────────────────────────────┐  ┌──────────────────────────────────────┐ │
│  │  SYSTEM HEALTH                  │  │  EXECUTION HINTS (Live)              │ │
│  │                                 │  │                                      │ │
│  │  Data Feed:       🟢 Healthy   │  │  NIFTY25JAN26300CE                   │ │
│  │  Last Update:     2ms ago      │  │  Posture:  ⏸️ WAIT                    │ │
│  │  WebSocket:       🟢 Connected │  │  Reason:   Feed stale (>5s)          │ │
│  │  Backend:         🟢 Healthy   │  │                                      │ │
│  │                                 │  │  NIFTY25JAN26400CE                   │ │
│  │  Subscriptions:   12 symbols   │  │  Posture:  🟢 PASSIVE                 │ │
│  │  Strategies:      3 active     │  │  Reason:   Spread tight, OFI neutral │ │
│  │                                 │  │                                      │ │
│  └─────────────────────────────────┘  └──────────────────────────────────────┘ │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  MARKET DEPTH (Selected: NIFTY25JAN26300CE)                               │  │
│  │                                                                           │  │
│  │  BIDS                    │ LTP: 285.50 │                    ASKS          │  │
│  │  ─────────────────────────┼─────────────┼─────────────────────────────────│  │
│  │  ████████████  225 @ 284.0│             │ 287.0 @ 975  ████████████████   │  │
│  │  ██████████    225 @ 283.5│             │ 287.5 @ 300  ████               │  │
│  │  ████████      150 @ 283.0│             │ 288.0 @ 450  ██████             │  │
│  │  ██████        100 @ 282.5│             │ 288.5 @ 200  ███                │  │
│  │  ████           75 @ 282.0│             │ 289.0 @ 375  █████              │  │
│  │                                                                           │  │
│  │  Spread: ₹3.00 (1.05%)    Microprice: 284.74    Imbalance: +0.23          │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  [🔴 KILL ALL STRATEGIES]                                                      │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

**Components:**
- `ActiveStrategies` — List with live P&L, Greeks drift, kill button
- `SystemHealth` — Feed status, WebSocket, backend health
- `ExecutionHints` — From WebSocket `execution_hint`
- `DepthVisualization` — From WebSocket `depth_snapshot`
- `KillSwitch` — Emergency stop for all strategies

**Backend:** WebSocket provides `depth_snapshot`, `feature_snapshot`, `execution_hint`, `strategy_update`

---

### 🔴 Page 9: REPLAY MODE (PRIMARY FOCUS)

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  ⚡ PAYOFF ENGINE          [Strategy] [Analyze] [Screener] [Risk] [Replay ●]   │
├─────────────────────────────────────────────────────────────────────────────────┤
│  REPLAY: 2025-01-15 09:15:00 → 2025-01-15 15:30:00    Strategy: Bull Call Spread│
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  TIMELINE SCRUBBER                                    Speed: [1x ▾]      │  │
│  │  ══════════════════════════════════════════════════════════════════════  │  │
│  │  09:15  09:45  10:15  10:45  11:15  11:45  12:15  14:30  15:00  15:30    │  │
│  │    ●─────────────────────────────○─────────────────────────────────────● │  │
│  │    ▲                             ▲                                        │  │
│  │  Start                      CURRENT: 11:45                 End            │  │
│  │                                                                           │  │
│  │  [◀◀ -1m] [◀ -1t] [⏸ Pause] [▶ +1t] [▶▶ +1m]    [🔴 Record Insight]     │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  ┌──────────────────────────────────┐  ┌─────────────────────────────────────┐ │
│  │  MARKET STATE @ 11:45           │  │  WHAT YOU SAW @ 11:45 (No Future)   │ │
│  │  ─────────────────────────────── │  │  ─────────────────────────────────── │ │
│  │                                  │  │                                     │ │
│  │  NIFTY Spot:     26,320         │  │  Your Prediction:                   │ │
│  │  ATM IV:         14.8%          │  │  "If spot stays > 26,300 by EOD,    │ │
│  │  DTE:            7 days         │  │   strategy will profit +₹3,000"     │ │
│  │                                  │  │                                     │ │
│  │  Your Strategy P&L: -₹1,200     │  │  Breakeven:       26,380            │ │
│  │  Current Delta:      +0.48      │  │  Max Profit Zone: 26,500+           │ │
│  │  Theta Burn/day:     -₹125      │  │  Kill Zone:       < 26,200          │ │
│  │                                  │  │                                     │ │
│  │  🟡 Profit probability: 58%      │  │  Greeks-based outlook:              │ │
│  │                                  │  │  "Delta-positive, needs +80 move"   │ │
│  └──────────────────────────────────┘  └─────────────────────────────────────┘ │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  PAYOFF @ 11:45 vs ACTUAL OUTCOME @ 15:30                                │  │
│  │                                                                           │  │
│  │  P&L                                                                      │  │
│  │   ▲                      ┌─ What ACTUALLY happened                       │  │
│  │   │                      │     (Spot ended @ 26,480)                     │  │
│  │ +5k│              ┌─────────────────────                                 │  │
│  │   │             ╱ │       ▼                                              │  │
│  │   │           ╱   │   ★ Actual P&L: +₹4,200                              │  │
│  │ +2k│         ╱    │                                                       │  │
│  │   │        ╱      │   ─── Predicted payoff @ 11:45                       │  │
│  │   │──────╱────────│   ─── Actual curve @ 15:30                           │  │
│  │  0│     ╱│ BE: 26,380   ● Predicted P&L (if spot=26,480)                 │  │
│  │   │    ╱ │             ★ Actual P&L                                      │  │
│  │   │   ╱  │                                                               │  │
│  │-2k│──╱   │        Prediction @ 11:45: +₹3,800 (if 26,480)               │  │
│  │   │      │        Actual @ 15:30:     +₹4,200                            │  │
│  │   └──────┴──────────────────────────────────────────────────► Spot       │  │
│  │     25.5k  26k   26.4k  26.5k   27k                                      │  │
│  │                    ▲                                                      │  │
│  │              Spot @ 11:45                                                │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  DEVIATION ANALYSIS                                                       │  │
│  │                                                                           │  │
│  │  ┌─────────────────────┬─────────────────┬─────────────────┬───────────┐ │  │
│  │  │ Metric              │ @ 11:45 (Pred)  │ @ 15:30 (Actual)│ Deviation │ │  │
│  │  ├─────────────────────┼─────────────────┼─────────────────┼───────────┤ │  │
│  │  │ P&L                 │      +₹3,800    │      +₹4,200    │  +₹400 ✅ │ │  │
│  │  │ Delta               │       +0.48     │       +0.35     │  -0.13    │ │  │
│  │  │ IV                  │       14.8%     │       13.2%     │  -1.6% 📉 │ │  │
│  │  │ Breakeven           │      26,380     │      26,340     │  -40 ✅   │ │  │
│  │  │ Time Value Lost     │         —       │       -₹580     │  (Theta)  │ │  │
│  │  └─────────────────────┴─────────────────┴─────────────────┴───────────┘ │  │
│  │                                                                           │  │
│  │  📊 INSIGHT: Prediction was conservative by ₹400 (+10.5%)                 │  │
│  │  • IV dropped 1.6% → Vega loss of ~₹420 (partially offset gains)         │  │
│  │  • Delta drift from 0.48 to 0.35 as option moved ITM                     │  │
│  │  • Theta cost ₹580 over 3.75 hours, but Delta gains dominated            │  │
│  │                                                                           │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  MARKET EVENTS IN REPLAY WINDOW                                          │  │
│  │                                                                           │  │
│  │  11:52  🔵 OI_BUILDUP    26300 CE saw +120K OI                           │  │
│  │  12:15  🟠 IV_SPIKE      ATM IV jumped 0.8% in 2 min                     │  │
│  │  14:45  🔴 PRICE_GAP     NIFTY jumped +80 after news                     │  │
│  │  15:10  🔵 OI_UNWIND     26400 PE saw -200K OI                           │  │
│  │                                                                           │  │
│  │  [Filter: All ▾]  [Export Timeline]                                       │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  ┌─────────────────────────────────┐  ┌──────────────────────────────────────┐ │
│  │  RECORDED INSIGHTS (This Run)   │  │  REPLAY SETTINGS                     │ │
│  │                                  │  │                                      │ │
│  │  💡 11:52 - "OI buildup bullish" │  │  Start: [2025-01-15 09:15 ▾]        │ │
│  │  💡 14:45 - "News catalyst!"     │  │  End:   [2025-01-15 15:30 ▾]        │ │
│  │                                  │  │  Interval: [1 minute ▾]             │ │
│  │  [+ Add Insight]                │  │  [Load Session] [Save Session]       │ │
│  │                                  │  │                                      │ │
│  └──────────────────────────────────┘  └──────────────────────────────────────┘ │
│                                                                                 │
│  [📤 Export Replay Report]  [🔄 Reset to Start]  [📊 Compare Multiple Runs]    │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

**Components:**
- `TimelineScrubber` — Drag/seek through time, play/pause/step controls
- `MarketStatePanel` — Spot, IV, DTE, current P&L at selected time
- `PredictionPanel` — What you "would have seen" at time T (no lookahead)
- `PayoffComparisonChart` — Overlay predicted vs actual payoff curves
- `DeviationTable` — Side-by-side comparison of all metrics
- `InsightPanel` — Auto-generated plain English explanations
- `EventTimeline` — OI/IV/price events with markers
- `InsightRecorder` — User can note observations during replay
- `ReplaySettings` — Configure session, save/load replays

**Backend:** NEW endpoints needed:
- `POST /api/replay/strategy` — Get payoff snapshots over time
- `POST /api/replay/prediction` — Payoff as-of historical time T
- `POST /api/replay/compare` — Prediction vs actual deviation
- `GET /api/replay/events` — Market events in time range
- `POST /api/replay/session/*` — Session management

---

## Component Architecture

```
frontend/
├── app/                          # Next.js 14 App Router
│   ├── (dashboard)/
│   │   ├── strategy/page.tsx     # Strategy Builder
│   │   ├── analyze/
│   │   │   ├── page.tsx          # Payoff Curve (default)
│   │   │   ├── sensitivity/      # Sensitivity Maps
│   │   │   └── iv-surface/       # IV Surface 3D
│   │   ├── screener/
│   │   │   ├── page.tsx          # Main Screener
│   │   │   ├── chain/            # Option Chain
│   │   │   └── iv-surface/       # IV Surface 3D
│   │   ├── risk/page.tsx         # Risk Decomposition
│   │   ├── live/page.tsx         # Live Monitor
│   │   └── replay/               # 🔴 PRIMARY FOCUS
│   │       ├── page.tsx          # Replay Mode Main
│   │       └── [sessionId]/      # Saved replay sessions
│   ├── layout.tsx
│   └── providers.tsx
├── components/
│   ├── strategy/
│   │   ├── LegTable.tsx
│   │   ├── OptionChainPicker.tsx
│   │   ├── RiskWarnings.tsx
│   │   └── StrategySummary.tsx
│   ├── payoff/
│   │   ├── PayoffChart.tsx
│   │   ├── PayoffTable.tsx
│   │   └── ScenarioTabs.tsx
│   ├── sensitivity/
│   │   ├── DeltaSurface.tsx
│   │   ├── GammaHeatmap.tsx
│   │   ├── ThetaSurface.tsx
│   │   ├── VegaSurface.tsx
│   │   └── KillZoneOverlay.tsx
│   ├── screener/
│   │   ├── FilterBar.tsx
│   │   ├── InstrumentTable.tsx
│   │   ├── OptionChainGrid.tsx
│   │   ├── IVSurface3D.tsx
│   │   └── OIAnalysis.tsx
│   ├── live/
│   │   ├── ActiveStrategies.tsx
│   │   ├── SystemHealth.tsx
│   │   ├── ExecutionHints.tsx
│   │   ├── DepthVisualization.tsx
│   │   └── KillSwitch.tsx
│   ├── replay/                   # 🔴 PRIMARY FOCUS - REPLAY COMPONENTS
│   │   ├── TimelineScrubber.tsx      # Play/pause/seek/step controls
│   │   ├── MarketStatePanel.tsx      # Current market snapshot at time T
│   │   ├── PredictionPanel.tsx       # What you "saw" at time T (no future)
│   │   ├── PayoffComparisonChart.tsx # Overlay predicted vs actual curves
│   │   ├── DeviationTable.tsx        # Side-by-side metric comparison
│   │   ├── InsightPanel.tsx          # Auto-generated explanations
│   │   ├── EventTimeline.tsx         # OI/IV/price event markers
│   │   ├── InsightRecorder.tsx       # User note-taking during replay
│   │   ├── ReplaySettings.tsx        # Session config, save/load
│   │   └── PnLEvolutionChart.tsx     # P&L over time with annotations
│   └── shared/
│       ├── GreeksDisplay.tsx
│       ├── QuoteCard.tsx
│       ├── StaleDataBadge.tsx
│       └── Navbar.tsx
├── lib/
│   ├── api/
│   │   ├── client.ts
│   │   ├── payoff.ts
│   │   ├── greeks.ts
│   │   ├── screener.ts
│   │   └── replay.ts             # 🔴 NEW - Replay API client
│   ├── websocket/
│   │   ├── manager.ts
│   │   ├── handlers.ts
│   │   └── buffer.ts
│   ├── store/
│   │   ├── strategy.ts
│   │   ├── market.ts
│   │   ├── ui.ts
│   │   └── replay.ts             # 🔴 NEW - Replay session state
│   └── types/
│       ├── api.ts
│       ├── models.ts
│       ├── websocket.ts
│       └── replay.ts             # 🔴 NEW - Replay types
└── hooks/
    ├── useStrategy.ts
    ├── usePayoff.ts
    ├── useWebSocket.ts
    ├── useScreener.ts
    ├── useThrottle.ts
    ├── useReplay.ts              # 🔴 NEW - Replay controls hook
    └── useReplayComparison.ts    # 🔴 NEW - Prediction vs actual hook
```

---

## Core TypeScript Interfaces

```typescript
// models.ts
interface OptionLeg {
  type: 'CE' | 'PE';
  side: 'BUY' | 'SELL';
  strike: number;
  expiry?: string;
  qty: number;
  lot: number;
  premium: number;
  instrument_id?: number;
}

interface Greeks {
  delta: number;
  gamma: number;
  theta: number;
  vega: number;
  rho: number;
}

interface PayoffPoint {
  spot: number;
  pnl: number;
}

interface PayoffResult {
  strategy: string;
  underlying: string;
  spot: number;
  max_profit: number;
  max_loss: number;
  net_premium: number;
  breakevens: number[];
  points: PayoffPoint[];
  greeks: Greeks;
}

interface InstrumentSnapshot {
  instrument_id: number;
  tradingsymbol: string;
  underlying: string;
  exchange: string;
  strike: number;
  option_type: 'CE' | 'PE';
  expiry_ms: number;
  days_to_expiry: number;
  last_price: number;
  bid_price: number;
  ask_price: number;
  spread_pct: number;
  volume: number;
  open_interest: number;
  oi_change: number;
  iv: number;
  iv_percentile: number;
  delta: number;
  gamma: number;
  theta: number;
  vega: number;
  is_itm: boolean;
  is_atm: boolean;
  is_otm: boolean;
  timestamp: number;
}

interface DepthLevel {
  price: number;
  size: number;
  orders: number;
}

interface DepthSnapshot {
  type: 'depth_snapshot';
  symbol: string;
  instrument_id: number;
  capture_time_ms: number;
  bids: DepthLevel[];
  asks: DepthLevel[];
  trade: {
    last_price: number;
    last_qty: number;
    total_traded_quantity: number;
  };
  source: 'clickhouse' | 'kite_ws' | 'mock';
  is_partial: boolean;
  is_stale: boolean;
}

interface ExecutionHint {
  type: 'execution_hint';
  symbol: string;
  instrument_id: number;
  posture: 'PASSIVE' | 'AGGRESSIVE' | 'WAIT';
  reasons: string[];
}

interface SensitivitySurface {
  type: 'delta' | 'gamma' | 'theta' | 'vega' | 'pnl';
  spot_center: number;
  strike: number;
  iv: number;
  surface: Array<{
    spot: number;
    values: Array<{ days: number; value: number }>;
  }>;
}

// 🔴 REPLAY MODE TYPES (PRIMARY FOCUS)

interface ReplayConfig {
  strategy: Strategy;
  start_timestamp: number;
  end_timestamp: number;
  interval_ms: number;
  include_greeks: boolean;
}

interface ReplaySnapshot {
  timestamp: number;
  underlying_price: number;
  total_pnl: number;
  greeks: Greeks;
  leg_prices: Array<{
    strike: number;
    price: number;
    iv: number;
  }>;
}

interface ReplaySummary {
  initial_pnl: number;
  final_pnl: number;
  max_pnl: number;
  min_pnl: number;
  pnl_std_dev: number;
}

interface ReplayStrategyResponse {
  snapshots: ReplaySnapshot[];
  summary: ReplaySummary;
}

interface PredictionHorizon {
  days_forward: number;
  iv_shift_pct?: number;
}

interface PredictedPayoff {
  horizon: PredictionHorizon;
  predicted_payoff: PayoffPoint[];
  predicted_greeks: Greeks;
}

interface PredictionSnapshotResponse {
  as_of_timestamp: number;
  market_state_at_time: {
    underlying_price: number;
    atm_iv: number;
    days_to_expiry: number;
  };
  predictions: PredictedPayoff[];
}

interface DeviationMetrics {
  pnl_deviation: number;
  pnl_deviation_pct: number;
  breakeven_shift: number;
  iv_change: number;
  delta_drift: number;
  prediction_accuracy_score: number;
}

interface ComparisonResponse {
  prediction_timestamp: number;
  actual_timestamp: number;
  time_elapsed_hours: number;
  
  at_prediction_time: {
    underlying_price: number;
    predicted_pnl_at_current_spot: number;
    predicted_breakeven: number;
  };
  
  at_actual_time: {
    underlying_price: number;
    actual_pnl: number;
    actual_breakeven: number;
  };
  
  deviation: DeviationMetrics;
  insights: string[];
}

type MarketEventType = 'IV_SPIKE' | 'PRICE_GAP' | 'OI_BUILDUP' | 'OI_UNWIND' | 'VOLUME_SPIKE';

interface MarketEvent {
  timestamp: number;
  type: MarketEventType;
  description: string;
  magnitude: number;
  strike?: number;
  option_type?: 'CE' | 'PE';
}

interface ReplaySession {
  session_id: string;
  config: ReplayConfig;
  current_timestamp: number;
  progress_pct: number;
  market_state: {
    underlying_price: number;
    quotes: Record<string, number>;
  };
  payoff_snapshot: {
    total_pnl: number;
    greeks: Greeks;
  };
  has_more: boolean;
}

interface RecordedInsight {
  id: string;
  timestamp: number;
  text: string;
  tags?: string[];
}
```

---

## Design Guidelines

### Color Palette (Dark Theme)

```css
:root {
  --bg-primary: #0a0a0b;
  --bg-secondary: #141416;
  --bg-tertiary: #1c1c1f;
  
  --text-primary: #fafafa;
  --text-secondary: #a1a1aa;
  --text-muted: #71717a;
  
  --profit: #22c55e;
  --loss: #ef4444;
  --warning: #f59e0b;
  --info: #3b82f6;
  
  --accent: #8b5cf6;
  --border: #27272a;
}
```

### Typography

- **Numbers:** JetBrains Mono / Fira Code (monospace)
- **Labels:** Inter / IBM Plex Sans
- **Headers:** Space Grotesk

### Heatmap Color Scale

```
Loss acceleration → Neutral → Safe
    🔴 #ef4444  →  🟡 #fbbf24  →  🟢 #22c55e
    
Gamma intensity:
    🟦 #3b82f6 (low) → 🟨 #fbbf24 (med) → 🟥 #ef4444 (high) → ⬛ #18181b (kill zone)
```