# Options Strategy Builder & Payoff Engine (C++ Plan)

This plan implements the product spec in `docs/payoff/payoff_feature_spec.md`.

It is intentionally written in the same “institutional” engineering style as the market observatory docs: deterministic, auditable, and simulation-safe.

## 1) Core requirement: payoff realism (no cheating)

The payoff engine must be simulation-ready:

- live mode uses current market data
- simulation mode replays historical ticks
- **no future leakage**: the engine can only use information available up to the current tick/time

This is the same discipline as the observatory replay/stream pipeline.

## 2) Core domain model (suggested)

### Strategy legs

A strategy is a list of legs; legs can span expiries.

Suggested C++ structs:

- `enum class OptionType { Call, Put }`
- `enum class Side { Buy, Sell }`
- `struct OptionLeg { OptionType type; Side side; double strike; Date expiry; int64_t quantity; double premium; /* market or manual */ }`

Validation rules (must-have from spec):

- margin sufficiency (risk layer)
- lot size consistency (instrument master)
- quantity limits (exchange aware)

### Scenario parameters

- spot shift (±%)
- IV shift (±)
- time shift (days)
- gap up/down

Represent this as an immutable `Scenario` object.

## 3) Payoff outputs

### A) Expiry payoff (baseline)

Outputs:

- payoff curve: `(underlying_price, pnl)` samples
- max profit / max loss
- breakevens
- net credit/debit

### B) Time-dependent payoff (advanced)

Payoff varies with:

- time to expiry (theta)
- IV changes
- spot movement

Support curves for:

- Today
- T+1
- Custom date

Implementation baseline:

- Black–Scholes pricing for European options (baseline)
- deterministic inputs

Extensions (later):

- dividends/rates
- smile surface (if data available)

## 4) Greeks & risk analytics

### Greeks

Compute per-leg and aggregated:

- Delta, Gamma, Theta, Vega, Rho

Provide:

- Greeks vs spot
- Greeks vs time

### Risk metrics

- Probability of profit (POP) — must be model-driven
- Probability of breakeven
- Tail loss (5%, 1%)
- Max drawdown (simulated)

Important: if POP is model-based, you must make the distributional assumptions explicit and reproducible (parameters recorded in output).

## 5) Payoff lenses (explicit requirement)

Support multiple lenses (the output schema must record which lens was used):

- Expiry payoff
- Path-dependent payoff (stop-loss, barriers)
- Conditional payoff (volatility regime)
- Execution-adjusted payoff (slippage)
- Risk-adjusted payoff (drawdown-penalized)
- Counterfactual payoff (alternate decisions)

Design guidance:

- implement “expiry payoff” + “time-dependent payoff” first
- introduce the other lenses as wrappers that transform either:
  - the scenario path, or
  - the cashflow stream, or
  - the final metric aggregation

## 6) API contracts (C++ backend)

Keep the API shape aligned with the spec:

- `POST /strategy/build`
- `POST /payoff/calculate`
- `POST /greeks/calculate`
- `POST /scenario/run`
- `POST /simulation/replay`

Inputs (example):

```json
{
  "legs": [ ... ],
  "spot": 22500,
  "iv": 0.18,
  "time_to_expiry_days": 14,
  "scenario": { ... }
}
```

Outputs:

```json
{
  "payoff_curve": [ ... ],
  "breakevens": [ ... ],
  "max_profit": 12340,
  "max_loss": -5600,
  "greeks": { ... },
  "assumptions": { ... },
  "determinism": { "engine_version": "...", "seed": 0 }
}
```

## 7) Integration with market observatory

Where “reuse” is real (and valuable):

- **replay/stream discipline**: no future leakage, per-symbol watermarking
- **data-source abstraction**: ClickHouse primary, optional live failover
- **instrument master**: lot size, expiry/strike metadata, symbol resolution
- **auditability**: every output reproducible from an input record

This keeps the payoff platform desk-grade rather than a toy chart.
