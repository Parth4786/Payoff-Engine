# C++ Rebuild Docs (Market Observatory + Payoff Platform)

This folder is the **C++-first documentation pack** for rebuilding this project in C++ while reusing the **same engineering constraints** proven in the existing Python implementation.

Two product lines are covered:

1) **Execution Observatory** (snapshot-based L2 microstructure + execution intelligence)
2) **Options Strategy Builder & Payoff Analytics** (from the product spec in `docs/payoff/payoff_feature_spec.md`)

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

- `LESSONS_LEARNED.md` — the real problems we hit in live+replay and how the architecture solved them.
- `ARCHITECTURE.md` — proposed C++ module boundaries and data flow.
- `DATA_CONTRACT.md` — C++ structs + invariants for `DepthSnapshot`-style market data.
- `DATA_SHAPES.md` — concrete examples of how data looks in ClickHouse, Kite, and instrument master CSVs.
- `API_DTO_SCHEMA.md` — exact JSON payloads for snapshots/features/hints (frontend integration contract).
- `PORTING_CHECKLIST.md` — which Python files to port first + a ready-to-use translation prompt.
- `CONNECTIVITY.md` — how to connect to ClickHouse, and how to handle Kite in a C++ build safely.
- `PAYOFF_ENGINE_PLAN.md` — how the payoff platform maps into the same replay/live discipline.
- `BUILD_AND_RUN.md` — recommended build system + dependency choices (CMake + vcpkg/Conan).

## Project intent

The C++ rebuild is not “rewrite for speed”. It’s a rewrite for:

- deterministic, auditable behavior
- better latency control
- safer concurrency and backpressure
- a clean API boundary that supports both **microstructure** and **payoff analytics** without leaking logic into the UI
