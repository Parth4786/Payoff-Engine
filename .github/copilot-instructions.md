# Copilot instructions (Payoff-Engine)

## Big picture
- This repo is a desk-grade, event-driven options analytics platform with two modes: **live** (Kite WS) and **replay** (ClickHouse). The core rule is **same downstream code path** for live and replay (no lookahead/leakage).
- Backend owns all math (features, IV, Greeks, payoff). The frontend is **render-only**.

## Code layout (C++ backend)
- `cpp/` is the C++20 workspace; CMake builds two libs and an executable:
  - `payoff_core`: models, datasource, streaming pipeline, cache, features, payoff/greeks
  - `payoff_api`: REST + WebSocket server and route/DTO glue
  - `payoff_engine`: entry point + CLI (`cpp/src/main.cpp`)
- Module map (see `cpp/include/*` + `cpp/src/*`):
  - Streaming pipeline: `cpp/src/streaming/{assembler,dedup,gap_detector,pipeline}.cpp`
  - Feature computation: `cpp/src/features/engine.cpp` (keep deterministic; no IO)
  - Execution hints: `cpp/src/execution/hint_engine.cpp` (safe posture includes `WAIT`)
  - Payoff/Greeks: `cpp/src/payoff/{pricing,calculator,greeks,sensitivity}.cpp`
  - API boundary: `cpp/src/api/{rest_server,websocket_server,routes,dto}.cpp`

## Non-negotiable contracts & invariants
- Canonical symbol identity is `{exchange}:{exchange_token}` (example: `NFO:49543`). Treat `instrument_id` as the **exchange token** (do not invent alternative IDs).
- Canonical market snapshot is `DepthSnapshot` (see `docs/API_DTO_SCHEMA.md` and `docs/DATA_CONTRACT.md`). Enforce invariants server-side:
  - bids sorted **DESC**, asks sorted **ASC**
  - best bid `<` best ask
- Data-quality flags must be explicit in payloads: `source` in `{clickhouse|kite_ws|mock}`, plus `is_partial` / `is_stale`.
- Replay determinism: do not read beyond the current watermark; simulations must degrade to live realism.

## Build / run / test workflows
- C++ build (Windows-friendly):
  - Configure: `cmake -S cpp -B cpp/build -G Ninja -DCMAKE_BUILD_TYPE=Release` (or MSVC generator)
  - Build: `cmake --build cpp/build --config Release`
  - Run API server: `cpp/build/payoff_engine --serve --port 8080`
  - Tests: `ctest --test-dir cpp/build --output-on-failure` (GTest tests only if GTest is found; `test_integration` always exists)
- vcpkg integration: `cpp/CMakeLists.txt` auto-uses `$VCPKG_ROOT` if set.
- Frontend (Next.js 14):
  - `cd frontend && npm install && npm run dev`
  - `npm run type-check` for TS.

## API / DTO conventions
- Keep wire shapes aligned with the docs:
  - `docs/API_DTO_SCHEMA.md` (DTOs)
  - `docs/payoff-engine-architecture/API_REFERENCE.md` (REST)
  - `docs/payoff-engine-architecture/WEBSOCKET_PROTOCOL.md` (WS)
- When editing or adding endpoints, prefer using `nlohmann::json` for parsing/serialization (the simplified string parsing in `cpp/src/api/routes.cpp` is intentionally minimal).

## Frontend integration expectations
- The app redirects to `/strategy` from `frontend/app/page.tsx`; hooks live in `frontend/hooks/` and WebSocket logic in `frontend/lib/websocket/`.
- Frontend must treat backend `is_stale`/`is_partial` as first-class state and avoid “recomputing” backend-derived values client-side.
