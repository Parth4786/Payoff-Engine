# Lessons Learned (Python → C++ rebuild)

This document captures the major engineering problems encountered in the Python implementation and the patterns used to solve them.

These are the same problems the C++ rebuild must anticipate.

## 1) Live feeds are noisy and will lie

### Problem
Even when a data vendor says “FULL depth”, reality includes:

- partial depth updates (only bids or only asks)
- duplicate ticks
- out-of-order timestamps
- bursts (high-rate tick storms)
- silent gaps (no updates for hundreds of ms)

If you assume a “nice stream”, features become unstable and the UI becomes misleading.

### Solution pattern
Build the pipeline defensively:

- **Normalizer**: convert each source into a canonical model.
- **Assembler**: merge partial updates into a coherent book state per symbol.
- **Dedup + ordering**: drop duplicates and drop regressions (do not “reinsert”).
- **Gap detector**: declare `is_stale` when gaps exceed thresholds.
- **Safe degrade**: suppress unsafe features and set execution posture to `WAIT`.

Reference: `docs/STREAMING_ENGINE.md` and `docs/DATA_CONTRACT.md`.

## 2) Replay/live parity is sacred

### Problem
Teams often implement replay and live as separate paths (separate code, separate bugs). This causes:

- “works in replay, breaks live”
- inconsistent feature output
- inability to debug incidents using historical data

### Solution pattern
Force convergence:

- all sources produce the same canonical `DepthSnapshot`
- **replay and streaming share the exact same downstream pipeline**
- parity tests compare replay vs streaming feature output for the same snapshot sequence

This is more important than micro-optimizing latency.

## 3) Identity mapping is the #1 operational foot-gun

### Problem
Kite and ClickHouse identify instruments differently.

- ClickHouse `instrument_id` is treated as **Kite instrument master `exchange_token`**.
- Kite WebSocket subscription uses **`instrument_token`**.

If you confuse these or “guess” mappings, you stream the wrong instrument and silently corrupt analytics.

### Solution pattern
- Canonical internal id is `DepthSnapshot.instrument_id`.
- Enforce mapping: `instrument_token -> exchange_token`.
- If mapping is missing: **log and drop** (never guess).

Reference: `docs/KITE_FALLBACK.md`.

## 4) Time semantics must be explicit

### Problem
Different timestamps have different precision and meaning:

- ClickHouse `capture_time` can be `DateTime64(6)`.
- Vendor timestamps may be only second-precision.

If you mix these inconsistently, you get:

- broken ordering
- false gap detection
- incorrect snapshot deltas

### Solution pattern
- Treat `capture_time` as authoritative ordering key.
- Reason at **millisecond granularity** for staleness/UI timeline.
- Truncate/floor exchange timestamps to second precision when normalizing.

Reference: `docs/DATA_CONTRACT.md`.

## 5) UI must surface uncertainty, not hide it

### Problem
A dashboard that always looks “clean” is dangerous in trading.

### Solution pattern
Expose uncertainty in payloads:

- `source: clickhouse | kite_ws`
- `is_partial`, `is_stale`
- dropped duplicates / reorder counters

Then apply explicit rules:

- `is_stale=True` ⇒ execution posture `WAIT`
- partial depth ⇒ suppress microprice and other fragile features

Reference: `docs/OPERATIONS_RUNBOOK.md`.

## 6) Performance isn’t just speed — it’s backpressure

### Problem
During bursts, a system that tries to push every tick to the UI will:

- fall behind
- blow buffers
- appear “frozen”

### Solution pattern
- compute features on every tick (for parity)
- downsample UI emissions (e.g., every 50ms)
- keep per-symbol rolling windows bounded

## 7) No-future-leakage in simulation is a product requirement

The payoff platform spec explicitly requires:

> Simulation must behave as if the next tick does not exist even if stored.

### Implication for C++
- replay/simulation must stream events strictly in time order
- downstream logic must not peek beyond current watermark
- all results must be reproducible from (inputs + config + seed)

This is the same discipline as the execution observatory replay.

---

## Summary: what we keep in the C++ rebuild

- Canonical contracts and invariants (drop invalid snapshots)
- Source abstraction
- Assembler + dedup + gap detector
- Parity as a first-class test goal
- Determinism + auditability as first-class product behavior
