# Connectivity & Integration (C++)

This document explains how the C++ rebuild should connect to existing infrastructure (ClickHouse, instrument master), and how to handle Kite in a safe, auditable way.

For concrete examples of the upstream payloads/rows and CSV headers, see:

- [DATA_SHAPES.md](DATA_SHAPES.md)

## 1) ClickHouse (primary source)

Current source of truth in this repo:

- Database: `tick_data_db`
- Table: `market_data`
- Ordered by: `capture_time`
- Identity: `instrument_id`

### Replay query pattern

Rules:

- always query a bounded time window
- always `ORDER BY capture_time`
- normalize rows into canonical `DepthSnapshot`

### Streaming poll pattern (watermark)

Rules:

- maintain a per-symbol watermark `last_capture_time`
- query strictly greater than watermark
- drop duplicates by `(instrument_id, capture_time)`

This matches `docs/OPERATIONS_RUNBOOK.md`.

### C++ client options

You can connect via either:

- native protocol C++ client library, or
- HTTP interface (simpler deployment, higher overhead)

Design requirement: whichever you choose, the datasource must implement the same `MarketDataSource` interface and emit canonical `DepthSnapshot` objects.

## 2) Instrument master (required for identity correctness)

The instrument master CSVs in this repo are the canonical mapping layer:

- `docs/kite_inspiration/instrument master/*.csv`

Required indexes in memory:

- `instrument_token -> exchange_token`
- `instrument_token -> tradingsymbol`
- `tradingsymbol -> instrument_token`

If a lookup fails:

- log and reject/drop (never guess)

## 3) Kite WebSocket (live failover)

Important reality:

- This repo’s Kite integration is Python-first.
- A direct C++ implementation is possible, but only if you have the **official** WS protocol details and auth flow available for C++.

### Recommended approach for a production-safe C++ rebuild

Run Kite as a **sidecar adapter service** (can reuse existing Python code), and make the C++ core data-source agnostic.

Pattern:

- Python sidecar connects to Kite and converts ticks into canonical `DepthSnapshot` JSON (or Protobuf).
- C++ core consumes these canonical messages as a `MarketDataSource` implementation.

Why this is recommended:

- avoids reverse engineering vendor protocols
- keeps auth/token flows in the ecosystem where they are already proven
- preserves the non-negotiable: downstream logic is identical

### If implementing Kite WS directly in C++

You still must preserve:

- canonical identity: `instrument_id = exchange_token`
- drop on mapping misses
- partial updates treated as partial even in “full mode”
- assembler/dedup/gap detection behavior identical to ClickHouse pipeline

Reference behavior requirements:

- `docs/KITE_FALLBACK.md`
- `docs/STREAMING_ENGINE.md`

## 4) Per-symbol failover policy

Failover is per symbol:

- primary: ClickHouse
- secondary: Kite (via sidecar or direct WS)

Triggers:

- repeated ClickHouse stream errors
- persistent staleness
- silence while market expected active

Behavior:

- switch only that symbol
- emit explicit `source` change in logs and payloads
- downstream remains identical

## 5) Serialization

For canonical message exchange between components, choose one:

- JSON (fast to iterate; heavier)
- Protobuf/FlatBuffers (faster; strict schema)

Regardless, do not leak source-specific fields into downstream logic.
