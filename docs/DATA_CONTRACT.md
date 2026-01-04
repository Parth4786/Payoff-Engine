# C++ Data Contract (Market Data)

This is the C++ translation of the canonical contract described in `docs/DATA_CONTRACT.md`.

The purpose is the same: **one canonical shape**, explicit uncertainty, and deterministic replay/stream behavior.

## 1) Canonical structs (example)

Note: actual header layout is a build decision; this shows intent and invariants.

```cpp
// core/models/depth.hpp

#include <cstdint>
#include <string>
#include <vector>

// Use a real time type in implementation (e.g., std::chrono::time_point).
// This doc keeps it conceptual.
struct Timestamp {
  // millis since epoch (recommended canonical internal representation)
  int64_t epoch_ms;
};

struct DepthLevel {
  double price;     // > 0
  int64_t size;     // >= 0
  int64_t orders;   // >= 0
};

struct TradeInfo {
  // Optional fields: use std::optional in code.
  double last_price;              // optional
  int64_t last_qty;               // optional
  int64_t total_traded_quantity;  // optional
  double average_traded_price;    // optional
  int64_t total_buy_quantity;     // optional
  int64_t total_sell_quantity;    // optional

  int64_t oi;                     // optional
  int64_t oi_day_high;            // optional
  int64_t oi_day_low;             // optional

  Timestamp last_trade_time;      // optional, second precision
  Timestamp exchange_timestamp;   // optional, second precision

  double ohlc_open;               // optional
  double ohlc_high;               // optional
  double ohlc_low;                // optional
  double ohlc_close;              // optional
};

enum class Source {
  Clickhouse,
  KiteWs,
  Mock,
};

struct DepthSnapshot {
  // Canonical internal identifier:
  // ClickHouse instrument_id == Kite exchange_token
  uint32_t instrument_id;

  std::string symbol;

  // Authoritative ordering key.
  Timestamp capture_time; // millisecond precision reasoning

  std::vector<DepthLevel> bids; // sorted DESC by price
  std::vector<DepthLevel> asks; // sorted ASC by price

  TradeInfo trade;

  Source source;
  bool is_partial;
  bool is_stale;
};
```

## 2) Sorting & structural invariants

The following must always hold before feature computation:

- `bids.size() >= 1`, `asks.size() >= 1`
- `bids[0].price < asks[0].price`
- bids strictly descending by price
- asks strictly ascending by price

If violated:

- the snapshot is invalid
- it must be dropped
- execution posture must degrade safely (`WAIT`)

## 3) Identity mapping (critical)

Canonical rule:

- `DepthSnapshot.instrument_id` is the **exchange token**.
- ClickHouse `instrument_id` == Kite instrument master `exchange_token`.
- Kite WS subscribes using `instrument_token`.
- Therefore, WS ingestion must map: `instrument_token -> exchange_token`.

If mapping is missing:

- log and drop (never guess)

See `docs/KITE_FALLBACK.md`.

## 4) Time semantics

- Treat `capture_time` as authoritative ordering key.
- Reason at millisecond granularity for ordering, staleness, UI timeline.
- Vendor timestamps (exchange timestamps) are treated as second precision; truncate/floor if higher precision arrives.

## 5) Partial updates

A snapshot is `is_partial=true` when:

- only bids or only asks updated
- fewer than expected depth levels present
- feed provides incremental updates

Partial snapshots must be merged with last known good state by the assembler.

## 6) Stale detection

A snapshot is `is_stale=true` when:

- `capture_time - last_seen_time > stale_threshold_ms`

Downstream effects:

- suppress fragile features (e.g., microprice)
- execution posture `WAIT`
- UI must show warning
