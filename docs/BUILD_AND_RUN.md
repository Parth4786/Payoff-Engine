# Build & Run (C++)

This is a recommended build/run approach for a Windows-first C++ implementation.

It is intentionally conservative and reproducible.

## 1) Build system

- CMake (required)
- Dependency manager: vcpkg or Conan (pick one; do not mix per target)

Suggested libraries:

- HTTP server: (choose one)
  - `Boost.Beast` (Boost.Asio)
  - `cpp-httplib` (simple; fewer features)
- JSON: `nlohmann/json`
- Logging: `spdlog`
- CLI/config: `cxxopts` + `.env` support or structured config file
- ClickHouse client:
  - native client library if available, or HTTP wrapper
- WebSocket:
  - `Boost.Beast` WS support

## 2) Repo layout (recommended)

Example:

- `cpp/` (new C++ workspace root)
  - `CMakeLists.txt`
  - `src/`
  - `include/`
  - `tests/`

This repo can keep Python as a reference implementation while C++ is built in parallel.

## 3) Configuration

Keep names aligned with the existing runbook:

- ClickHouse host/port/db/table
- `LIVE_MODE` equivalent (clickhouse / clickhouse_failover_kite / kite_only)
- staleness thresholds
- failover silence thresholds

Do not log secrets.

## 4) Local run workflow (suggested)

1) Start ClickHouse (if local) or verify connectivity
2) Run C++ API service
3) Validate:
   - `/symbols`
   - `/replay/*`
   - `/stream/*`
   - `/payoff/*`

## 5) Determinism rules

- same input JSON + same config must produce identical outputs
- record `engine_version` and `assumptions` in the response
- simulations must not read beyond the current watermark
