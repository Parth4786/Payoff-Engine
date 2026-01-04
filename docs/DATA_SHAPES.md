# Data Shapes (ClickHouse / Kite / Instrument Master)

This doc shows **what the data actually looks like** in each upstream system, and how it maps into the canonical internal model.

It is written to avoid “hallucinated fields” and to preserve replay/stream parity.

## 1) ClickHouse (historical + primary live)

**DB/Table:** `tick_data_db.market_data`

### 1.1 Row shape (wide snapshot)

ClickHouse stores each snapshot as a **wide row** with explicit top-5 depth columns.

Representative fields (subset):

- Identity
  - `instrument_id` (UInt32) — treated as **exchange_token**
  - `exchange` (Int) — exchange code (example data shows `2`)
  - `feed_type` (String) — typically `Depth`

- Time
  - `capture_time` (DateTime64) — authoritative ordering key
  - `date` (Date)

- Depth (Top-5)
  - `bid_price_0..4`, `bid_size_0..4`, `bid_orders_0..4`
  - `ask_price_0..4`, `ask_size_0..4`, `ask_orders_0..4`

- Trade/L1 context (subset)
  - `last_price`
  - `last_traded_quantity`
  - `total_traded_quantity`
  - `average_traded_price`
  - `total_buy_quantity`, `total_sell_quantity`
  - `exchange_timestamp`, `last_update_time`, `last_traded_time`

- Daily OHLC (subset)
  - `open_price`, `high_price`, `low_price`, `close_price`, `percent_change`

- Optional enrichment columns (not reliable as canonical ingestion inputs)
  - `tradingsymbol`, `name`, `expiry`, `strike`, `tick_size`, `lot_size`, `instrument_type`

### 1.2 Example row (captured fixture)

From `docs/assets/clickhouse_depth_row_example.txt`:

```text
instrument_id	35219
exchange	2
feed_type	Depth
last_price	6.75
capture_time	2025-05-09 11:55:51.638302
...
bid_price_0	6.7
bid_size_0	225
bid_orders_0	1
...
ask_price_0	6.8
ask_size_0	975
ask_orders_0	3
...
exchange_timestamp	2025-05-09 11:55:51.000000
last_traded_time	2025-05-09 11:55:51.000000
open_price	7.65
high_price	8.4
low_price	6.2
close_price	8.15
```

### 1.3 Mapping into canonical internal model

- `DepthSnapshot.instrument_id = instrument_id` (ClickHouse)
- `DepthSnapshot.capture_time = capture_time`
- `DepthSnapshot.bids[i] = (bid_price_i, bid_size_i, bid_orders_i)`
- `DepthSnapshot.asks[i] = (ask_price_i, ask_size_i, ask_orders_i)`
- `DepthSnapshot.trade.*` from the trade/L1 fields when present
- `source = clickhouse`

## 2) Kite (live failover)

Kite identifies instruments by **instrument_token** on streaming APIs, but our canonical id must be **exchange_token**.

### 2.1 WebSocket packet fields (vendor shape)

Per `docs/kite_inspiration/kite_websocket_doc.md`, the binary packet contains (subset):

- `instrument_token`
- prices/volume/quantities
- timestamps
- open interest
- 10 market depth entries (5 bid + 5 ask)

Important: prices are scaled (typically paise) in the raw binary protocol; client libraries usually convert to floats.

### 2.2 Practical JSON-like shape (what the normalizer expects)

In this repo’s Python adapter (`data_sources/kite_ws_source.py`), ticks are treated as dictionaries shaped like:

```json
{
  "instrument_token": 408065,
  "last_price": 1412.95,
  "last_quantity": 5,
  "volume": 7360198,
  "average_traded_price": 1412.47,
  "buy_quantity": 0,
  "sell_quantity": 5191,
  "oi": 0,
  "oi_day_high": 0,
  "oi_day_low": 0,
  "last_trade_time": "2021-06-08 15:45:52",
  "exchange_timestamp": "2021-06-08 15:45:56",
  "ohlc": { "open": 1396, "high": 1421.75, "low": 1395.55, "close": 1389.65 },
  "depth": {
    "buy":  [ {"price": 1412.90, "quantity": 10, "orders": 2}, ... up to 5 ],
    "sell": [ {"price": 1412.95, "quantity": 5191, "orders": 13}, ... up to 5 ]
  }
}
```

Notes:

- Many fields are optional; absence must be handled.
- Depth is required to build a `DepthSnapshot` in the current adapter.
- Timestamps are treated as **second-precision**.

### 2.3 Mapping into canonical internal model

- subscribe using `instrument_token`
- lookup `exchange_token` via instrument master
- set canonical id:
  - `DepthSnapshot.instrument_id = exchange_token`
- set `DepthSnapshot.symbol = "{exchange}:{exchange_token}"` (canonical symbol)
- build top-5 bids/asks from `depth.buy/sell`
- set `source = kite_ws`

If mapping is missing: **log and drop** (never guess).

## 3) Instrument Master (InstrumentManager)

The instrument master is the deterministic mapping layer used to:

- resolve user input (tradingsymbol) into subscription tokens
- map Kite subscription id → canonical internal id
- enforce exchange-aware metadata like lot size and tick size

### 3.1 CSV header shape (as stored in this repo)

Representative headers (NSE/NFO variants):

```csv
instrument_token,exchange_token,tradingsymbol,name,last_price,expiry,strike,tick_size,lot_size,instrument_type,segment,exchange
```

Some CSV variants also contain extra columns like:

- leading unnamed index column (pandas export)
- `month`

### 3.2 What InstrumentManager builds

From `core/instrument_manager.py`, the loader materializes these lookups:

- `instrument_token -> InstrumentInfo`
- `exchange_token -> InstrumentInfo` (legacy; can conflict across exchanges)
- `canonical_symbol (exchange:exchange_token) -> InstrumentInfo` (preferred)
- `tradingsymbol -> list[InstrumentInfo]` (options will have many rows)

The canonical identity rule is:

- `canonical_symbol = "{exchange}:{exchange_token}"`

### 3.3 Why canonical_symbol exists

The same `exchange_token` can appear across different exchanges.

Keying by `exchange:exchange_token` avoids collisions and prevents streaming the wrong instrument.
