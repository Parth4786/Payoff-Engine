# C++ Rebuild Docs (Market Observatory + Payoff Platform)

This folder is the **C++-first documentation pack** for rebuilding this project in C++ while reusing the **same engineering constraints** proven in the existing Python implementation.

Two product lines are covered:

1) **Execution Observatory** (snapshot-based L2 microstructure + execution intelligence)
2) **Options Strategy Builder & Payoff Analytics** (from the product spec in `docs/payoff/payoff_feature_spec.md`)

## 🚀 START HERE

- **[BOOTSTRAP_PROMPT.md](BOOTSTRAP_PROMPT.md)** — Complete prompt to paste into an LLM to kick off the project with task checklist.

## Non-negotiables (carry over as hard requirements)

These are treated as **bugs if violated**:

- **Replay and streaming use identical downstream logic**.
- **Frontend never computes features** (render-only; backend owns math).
- **Backend is data-source agnostic** (all feeds normalize into one internal model).
- **Append-only, time-ordered** data.
- **Safe degradation is mandatory**: returning `WAIT` is always valid.
- **Data reality is snapshot L2** (Top-5 only; no order-level events).

See the original canonical docs:
- `docs/DATA_CONTRACT.md`
- `docs/STREAMING_ENGINE.md`
- `docs/KITE_FALLBACK.md`
- `docs/ENGINEERING_PLAYBOOK.md`

## What to read next

| Doc | Purpose |
|-----|---------|
| [BOOTSTRAP_PROMPT.md](BOOTSTRAP_PROMPT.md) | **START HERE** — Full LLM prompt with task checklist |
| `docs/DATA_CONTRACT.md` | Canonical model invariants |
| `docs/payoff/payoff_feature_spec.md` | Payoff platform requirements |
| `docs/STREAMING_ENGINE.md` | Defensive streaming rules |
| `docs/KITE_FALLBACK.md` | Identity mapping rules |

## Key Python files to reference

| Category | Files |
|----------|-------|
| Core contracts | `core/models.py`, `core/datasource.py`, `core/instrument_manager.py` |
| Data sources | `data_sources/clickhouse_source.py`, `data_sources/kite_ws_source.py` |
| Streaming | `streaming/assembler.py`, `streaming/dedup.py`, `streaming/gap_detector.py` |
| Features | `features/engine.py` |
| Execution | `execution/hint_engine.py`, `execution/models.py` |

## Project intent

The C++ rebuild is not "rewrite for speed". It's a rewrite for:

- deterministic, auditable behavior
- better latency control
- safer concurrency and backpressure
- a clean API boundary that supports both **microstructure** and **payoff analytics** without leaking logic into the UI


