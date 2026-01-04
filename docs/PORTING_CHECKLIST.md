# Porting Checklist (Python → C++)

This doc lists the **Python files that are most valuable to copy/translate** into the C++ implementation.

The goal is not to port “everything”. The goal is to preserve the **correctness architecture**:

- replay and streaming share identical downstream logic
- one canonical internal model
- defensive streaming (merge/dedup/gaps)
- explicit uncertainty
- deterministic, auditable outputs

## 1) Must-port (core contracts)

These define the canonical model and rules. Port these first.

- [core/models.py](core/models.py) — `DepthLevel`, `TradeInfo`, `DepthSnapshot`, validation invariants.
- [core/datasource.py](core/datasource.py) — `MarketDataSource` interface (replay + stream).
- [core/instrument_manager.py](core/instrument_manager.py) — instrument master CSV parsing + identity mapping (token→exchange_token) + canonical symbol.

## 2) Must-port (data sources)

These encode ClickHouse row shape, Kite tick normalization, and failover rules.

- [data_sources/clickhouse_source.py](data_sources/clickhouse_source.py)
  - `REPLAY_COLUMNS` (real table schema subset)
  - `parse_canonical_symbol`/`make_canonical_symbol`
  - replay query pattern (bounded time window, ordered)
  - streaming watermark polling pattern
  - row → `DepthSnapshot` normalization
- [data_sources/kite_ws_source.py](data_sources/kite_ws_source.py)
  - `instrument_token -> exchange_token` mapping enforcement
  - tick → `DepthSnapshot` normalization
  - “drop on missing mapping” behavior
- [data_sources/failover_source.py](data_sources/failover_source.py)
  - per-symbol failover selection before downstream pipeline
- [data_sources/mock_source.py](data_sources/mock_source.py)
  - minimal deterministic source for tests

## 3) Must-port (streaming correctness stages)

These files are the defensive streaming spine; they’re where most real-life bugs live.

- [streaming/assembler.py](streaming/assembler.py) — merge partial updates into full book state.
- [streaming/dedup.py](streaming/dedup.py) — duplicate & out-of-order policy.
- [streaming/gap_detector.py](streaming/gap_detector.py) — staleness and gap thresholds.
- [streaming/rolling.py](streaming/rolling.py) — bounded rolling windows.
- [streaming/pipeline.py](streaming/pipeline.py) and [streaming/hub.py](streaming/hub.py) — orchestration/routing.

## 4) Must-port (feature engine)

The feature engine must stay pure/deterministic.

- [features/engine.py](features/engine.py)

## 5) Recommended port (execution hints)

- [execution/hint_engine.py](execution/hint_engine.py)
- [execution/models.py](execution/models.py)

Even if you change hint rules later, keep the structure:

- `WAIT` always valid
- reasons are explicit

## 6) API reference (optional for porting, useful for compatibility)

If you want endpoint compatibility, use these as reference:

- [api/app.py](api/app.py)
- [core/payloads.py](core/payloads.py)

If the C++ API is new, still keep the DTO shape in [docs/cpp/API_DTO_SCHEMA.md](docs/cpp/API_DTO_SCHEMA.md).

## 7) Tests to copy as behavioral specs

These are useful as “expected behavior” while porting:

- [tests/test_clickhouse_source.py](tests/test_clickhouse_source.py)
- [tests/test_api.py](tests/test_api.py)

(There may be more; treat tests as executable documentation.)

---

## Translation prompt (paste into an LLM)

Use this prompt for **one file at a time** (best results), starting from section 1.

```
You are a senior C++ engineer. Translate the following Python module into production-grade C++20.

Hard requirements:
- Preserve behavior exactly; do not change semantics.
- No future leakage: any replay/streaming logic must only use current/past data.
- Enforce the data-contract invariants: bids/asks ordering, best_bid < best_ask, non-negative sizes.
- No global mutable state.
- Explicit error handling (use error types or expected/optional; no silent failure).
- Deterministic outputs: same inputs -> same outputs.
- Keep the API surface minimal and clean.

Output requirements:
- Provide .hpp and .cpp (or header-only if justified).
- Use strong types for time (std::chrono) and optional fields (std::optional).
- Provide brief unit tests (Catch2 or GoogleTest) for the critical invariants and one happy-path example.
- Include a short note explaining any C++-specific choices (e.g., chrono representation, JSON library assumptions).

Here is the Python module to translate:

<PASTE PYTHON FILE CONTENT HERE>
```

## Porting order (recommended)

1) models → datasource interface → instrument master
2) clickhouse source (replay first)
3) features engine
4) streaming assembler/dedup/gap
5) kite adapter + failover
6) API layer
