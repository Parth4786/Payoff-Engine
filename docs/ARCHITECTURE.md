# C++ Architecture (Proposed)

This document proposes a C++ architecture that preserves the proven invariants from the Python system.

## 1) High-level data flow

Same conceptual flow as `docs/ARCHITECTURE.md`:

Raw Feed (ClickHouse live OR Kite WS)
→ Source Adapter (per-source)
→ Normalizer
→ Snapshot Assembler (merge partial updates)
→ Dedup/Order
→ Feature Engine
→ Execution Hint Engine
→ API (HTTP + WS/SSE)
→ Frontend (render only)

Key rule: **source selection happens before the assembler** so downstream is identical.

## 2) Module boundaries (C++ packages)

Recommended layout (example):

- `core/`
  - `models/` — POD structs (`DepthSnapshot`, `DepthLevel`, `TradeInfo`)
  - `time/` — timestamp conversions + time semantics
  - `logging/` — structured logging helpers
  - `errors/` — explicit error types

- `datasource/`
  - `MarketDataSource` interface (replay + stream)
  - `clickhouse/` implementation
  - `kite_sidecar/` or `kite_ws/` implementation (see `CONNECTIVITY.md`)
  - `failover/` per-symbol failover wrapper

- `streaming/`
  - assembler (partial merge)
  - dedup + ordering
  - gap detection
  - rolling window/rate limiting for UI

- `features/`
  - pure deterministic computations from `DepthSnapshot -> FeatureSnapshot`

- `execution/`
  - hint engine: `PASSIVE | AGGRESSIVE | WAIT` with explainable reasons

- `payoff/`
  - option leg/strategy model
  - pricing engine (Black-Scholes baseline)
  - payoff curve generator
  - scenario runner
  - risk metrics

- `api/`
  - REST handlers (`/symbols`, `/replay/*`, `/payoff/*`, ...)
  - WS/SSE streaming handlers
  - DTO serialization layer (JSON)

## 3) Concurrency model

Goals:

- deterministic ordering per symbol
- no global mutable state
- controlled backpressure

Suggested model:

- one per-symbol stream processor (single-threaded per symbol) for strict ordering
- a shared thread pool for IO (ClickHouse fetch, WS reads)
- message passing (lock-free queue or bounded channel) into per-symbol processors

Do not allow multiple threads to mutate the same `BookState`.

## 4) Contracts & determinism

- Canonical market contract: `DepthSnapshot` (see `DATA_CONTRACT.md`).
- Pricing/payoff contract: deterministic (same input -> same output).
- Simulation contract: no future leakage; all replay streams are strictly ordered.

## 5) API layering

The API must not expose source-specific shapes.

- Inbound requests: strategies, scenarios, symbol selection.
- Outbound payloads: canonical snapshots/features/hints/payoff curves.

If needed, add a `source` field (`clickhouse | kite_ws | mock`) and data-quality flags (`is_partial`, `is_stale`).

## 6) Testing targets

Minimum test pillars to keep the system honest:

- **contract invariants**: bid/ask sorting, best bid < best ask
- **merge correctness**: partial updates merge as expected
- **dedup/order**: duplicates dropped; time regressions dropped
- **parity**: replay vs streaming produce the same feature output for the same snapshot sequence
- **payoff determinism**: same strategy/scenario -> identical curve + greeks
- **simulation integrity**: no future leakage (watermark enforcement)
