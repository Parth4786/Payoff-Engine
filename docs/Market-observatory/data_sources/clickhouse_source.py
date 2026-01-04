from __future__ import annotations

import asyncio
import logging
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any, Protocol

from core.datasource import MarketDataSource
from core.models import DepthLevel, DepthSnapshot, TradeInfo, make_snapshot

logger = logging.getLogger(__name__)


# ClickHouse exchange column mapping to exchange names
CLICKHOUSE_EXCHANGE_MAP: dict[str, str] = {
    "1": "NSE",
    "2": "NFO",
}

# Explicit columns for replay queries (avoid SELECT * for performance)
REPLAY_COLUMNS: tuple[str, ...] = (
    "instrument_id",
    "exchange",
    "capture_time",
    # Depth levels (bid)
    "bid_price_0", "bid_size_0", "bid_orders_0",
    "bid_price_1", "bid_size_1", "bid_orders_1",
    "bid_price_2", "bid_size_2", "bid_orders_2",
    "bid_price_3", "bid_size_3", "bid_orders_3",
    "bid_price_4", "bid_size_4", "bid_orders_4",
    # Depth levels (ask)
    "ask_price_0", "ask_size_0", "ask_orders_0",
    "ask_price_1", "ask_size_1", "ask_orders_1",
    "ask_price_2", "ask_size_2", "ask_orders_2",
    "ask_price_3", "ask_size_3", "ask_orders_3",
    "ask_price_4", "ask_size_4", "ask_orders_4",
    # Trade info
    "last_price",
    "last_traded_quantity",
    "total_traded_quantity",
    "average_traded_price",
    "total_buy_quantity",
    "total_sell_quantity",
    "last_traded_time",
    "exchange_timestamp",
    "open_price",
    "high_price",
    "low_price",
    "close_price",
)


def parse_canonical_symbol(symbol: str) -> tuple[str, int]:
    """Parse canonical symbol (exchange:exchange_token) into components."""
    if ":" not in symbol:
        raise ValueError(
            f"Invalid canonical symbol format: {symbol}. "
            "Expected 'exchange:exchange_token' (e.g., 'NSE:738561')"
        )
    parts = symbol.split(":", 1)
    exchange = parts[0].upper()
    try:
        exchange_token = int(parts[1])
    except ValueError as e:
        raise ValueError(f"Invalid exchange_token in symbol {symbol}") from e
    return exchange, exchange_token


def make_canonical_symbol(exchange: str, exchange_token: int) -> str:
    """Create canonical symbol from components."""
    return f"{exchange.upper()}:{exchange_token}"


class ClickHouseQueryClient(Protocol):
    """Minimal query surface we depend on.

    This lets us unit test without a real ClickHouse server.
    """

    def query(self, query: str, parameters: dict[str, Any] | None = None) -> Any:  # pragma: no cover
        ...


class ClickHouseClientFactory(Protocol):
    """Factory for creating ClickHouse clients."""

    def __call__(self) -> ClickHouseQueryClient:  # pragma: no cover
        ...


@dataclass
class ClickHouseClientPool:
    """Thread-safe connection pool for ClickHouse clients.
    
    Uses asyncio.Queue to manage a fixed pool of clients.
    Clients are reused after queries complete.
    """
    
    factory: ClickHouseClientFactory
    size: int = 5
    # NOTE: asyncio primitives are bound to the event loop they are created in.
    # Uvicorn creates the running loop at startup, while apps are often imported
    # earlier. Creating Locks/Queues at import time can bind them to a different
    # loop and crash on first request.
    _pool: asyncio.Queue[ClickHouseQueryClient] | None = field(default=None, init=False, repr=False)
    _initialized: bool = field(default=False, init=False)
    _lock: asyncio.Lock | None = field(default=None, init=False, repr=False)
    
    async def _ensure_initialized(self) -> None:
        """Lazily initialize the pool on first use."""
        if self._initialized:
            return

        if self._lock is None:
            self._lock = asyncio.Lock()

        async with self._lock:
            if self._initialized:
                return

            pool: asyncio.Queue[ClickHouseQueryClient] = asyncio.Queue(maxsize=self.size)
            for _ in range(self.size):
                client = await asyncio.to_thread(self.factory)
                await pool.put(client)

            self._pool = pool
            self._initialized = True
            logger.info("ClickHouse connection pool initialized", extra={"size": self.size})
    
    @asynccontextmanager
    async def acquire(self):
        """Acquire a client from the pool, return it when done."""
        await self._ensure_initialized()
        if self._pool is None:
            raise RuntimeError("ClickHouse client pool not initialized")

        client = await self._pool.get()
        try:
            yield client
        finally:
            await self._pool.put(client)


@dataclass(frozen=True, slots=True)
class ClickHouseConfig:
    host: str
    port: int
    username: str | None
    password: str | None
    database: str
    table: str

    # Streaming tunables (latency vs load tradeoff)
    batch_size: int
    poll_interval_ms: int


@dataclass(slots=True)
class ClickHouseDataSource(MarketDataSource):
    """ClickHouse datasource (replay + incremental live reads).

    - Replay: bounded time range, ordered by capture_time.
    - Streaming: incremental reads via watermark (capture_time > watermark), ordered, limited.

    Notes on latency:
    - Uses `clickhouse_connect` client (sync) wrapped via `asyncio.to_thread`.
      This keeps our overall pipeline async while avoiding heavy custom HTTP parsing.
    - Keep batches small and poll interval low for near-real-time updates.
    - Uses a connection pool to reuse clients across queries.
    """

    pool: ClickHouseClientPool
    config: ClickHouseConfig

    async def get_symbols(self) -> list[str]:
        # Return canonical symbols (exchange:exchange_token)
        # ClickHouse has exchange column (1=NSE, 2=NFO)
        query = (
            f"SELECT DISTINCT exchange, instrument_id "
            f"FROM {self.config.database}.{self.config.table} "
            "ORDER BY exchange, instrument_id"
        )
        rows = await self._query_rows(query)
        out: list[str] = []
        for r in rows:
            instrument_id = r.get("instrument_id")
            exchange_code = str(r.get("exchange", "")).strip()
            if instrument_id is None:
                continue

            exchange_name = CLICKHOUSE_EXCHANGE_MAP.get(exchange_code)
            if exchange_name is None:
                logger.warning(
                    "Unknown exchange code in ClickHouse",
                    extra={"exchange_code": exchange_code, "instrument_id": instrument_id}
                )
                continue

            out.append(make_canonical_symbol(exchange_name, int(instrument_id)))
        return out

    async def replay_snapshots(
        self,
        *,
        symbol: str,
        start_time: datetime,
        end_time: datetime,
        max_rows: int = 10000,
    ) -> list[DepthSnapshot]:
        exchange, instrument_id = parse_canonical_symbol(symbol)

        # Map exchange name back to ClickHouse exchange code
        exchange_code = None
        for code, name in CLICKHOUSE_EXCHANGE_MAP.items():
            if name == exchange:
                exchange_code = code
                break

        if exchange_code is None:
            logger.warning(
                "Unknown exchange for ClickHouse query",
                extra={"exchange": exchange, "symbol": symbol}
            )
            return []

        # Use explicit columns instead of SELECT * for performance
        columns = ", ".join(REPLAY_COLUMNS)
        query = (
            f"SELECT {columns} "
            f"FROM {self.config.database}.{self.config.table} "
            "WHERE instrument_id = {instrument_id:UInt32} "
            "AND exchange = {exchange_code:String} "
            "AND capture_time >= {start_time:DateTime64(6)} "
            "AND capture_time <= {end_time:DateTime64(6)} "
            "ORDER BY capture_time ASC "
            "LIMIT {max_rows:UInt32}"
        )
        rows = await self._query_rows(
            query,
            {
                "instrument_id": instrument_id,
                "exchange_code": exchange_code,
                "start_time": start_time,
                "end_time": end_time,
                "max_rows": max_rows,
            },
        )

        snapshots: list[DepthSnapshot] = []
        for row in rows:
            snap = _row_to_snapshot(row, canonical_symbol=symbol)
            if snap is not None:
                snapshots.append(snap)
        return snapshots

    async def stream_snapshots(self, *, symbol: str) -> AsyncIterator[DepthSnapshot]:
        exchange, instrument_id = parse_canonical_symbol(symbol)

        # Map exchange name back to ClickHouse exchange code
        exchange_code = None
        for code, name in CLICKHOUSE_EXCHANGE_MAP.items():
            if name == exchange:
                exchange_code = code
                break

        if exchange_code is None:
            logger.warning(
                "Unknown exchange for ClickHouse streaming",
                extra={"exchange": exchange, "symbol": symbol}
            )
            return

        watermark: datetime | None = None
        while True:
            query, params = _build_incremental_query(
                database=self.config.database,
                table=self.config.table,
                instrument_id=instrument_id,
                exchange_code=exchange_code,
                watermark=watermark,
                batch_size=self.config.batch_size,
            )

            try:
                rows = await self._query_rows(query, params)
            except Exception:
                logger.exception(
                    "ClickHouse incremental query failed",
                    extra={"symbol": symbol, "watermark": watermark},
                )
                await asyncio.sleep(self.config.poll_interval_ms / 1000.0)
                continue

            if not rows:
                await asyncio.sleep(self.config.poll_interval_ms / 1000.0)
                continue

            for row in rows:
                snap = _row_to_snapshot(row, canonical_symbol=symbol)
                if snap is None:
                    continue

                # Update watermark strictly (duplicates are handled downstream too, but
                # we keep the read path efficient).
                if watermark is None or snap.capture_time > watermark:
                    watermark = snap.capture_time

                yield snap

    async def _query_rows(
        self, query: str, parameters: dict[str, Any] | None = None
    ) -> list[dict[str, Any]]:
        async with self.pool.acquire() as client:
            def _run() -> list[dict[str, Any]]:
                # clickhouse_connect returns a QueryResult. We request dictionaries for explicitness.
                result = client.query(query, parameters=parameters)
                if hasattr(result, "named_results"):
                    return list(result.named_results())
                if isinstance(result, list):
                    # Allow fakes in tests
                    return result
                raise TypeError(f"Unexpected ClickHouse query result type: {type(result)!r}")

            return await asyncio.to_thread(_run)


def _build_incremental_query(
    *,
    database: str,
    table: str,
    instrument_id: int,
    exchange_code: str,
    watermark: datetime | None,
    batch_size: int,
) -> tuple[str, dict[str, Any]]:
    base = (
        "SELECT * "
        f"FROM {database}.{table} "
        "WHERE instrument_id = {instrument_id:UInt32} "
        "AND exchange = {exchange_code:String} "
    )

    params: dict[str, Any] = {"instrument_id": instrument_id, "exchange_code": exchange_code}

    if watermark is not None:
        base += "AND capture_time > {watermark:DateTime64(6)} "
        params["watermark"] = watermark

    base += "ORDER BY capture_time ASC "
    base += "LIMIT {batch_size:UInt32}"
    params["batch_size"] = int(batch_size)

    return base, params


def _row_to_snapshot(row: dict[str, Any], *, canonical_symbol: str | None = None) -> DepthSnapshot | None:
    """Convert a ClickHouse wide row into a canonical DepthSnapshot.

    If canonical_symbol is provided, it's used as-is. Otherwise, we construct it
    from the row's exchange and instrument_id columns.

    We intentionally ignore non-canonical enrichment columns like tradingsymbol/name/expiry.
    """

    try:
        instrument_id = int(row["instrument_id"])
        capture_time = row["capture_time"]
        if not isinstance(capture_time, datetime):
            # clickhouse-connect typically returns datetime for DateTime64
            raise TypeError("capture_time must be a datetime")

        # Determine canonical symbol
        if canonical_symbol is None:
            exchange_code = str(row.get("exchange", "")).strip()
            exchange_name = CLICKHOUSE_EXCHANGE_MAP.get(exchange_code)
            if exchange_name is None:
                logger.warning(
                    "Unknown exchange code in row; dropping snapshot",
                    extra={"exchange_code": exchange_code, "instrument_id": instrument_id}
                )
                return None
            canonical_symbol = make_canonical_symbol(exchange_name, instrument_id)

        bids = _parse_depth_side(row, side="bid")
        asks = _parse_depth_side(row, side="ask")
        if not bids or not asks:
            return None

        trade = TradeInfo(
            last_price=_maybe_positive_float(row.get("last_price")),
            last_qty=_maybe_int(row.get("last_traded_quantity")),
            total_traded_quantity=_maybe_int(row.get("total_traded_quantity")),
            average_traded_price=_maybe_positive_float(row.get("average_traded_price")),
            total_buy_quantity=_maybe_int(row.get("total_buy_quantity")),
            total_sell_quantity=_maybe_int(row.get("total_sell_quantity")),
            oi=None,
            oi_day_high=None,
            oi_day_low=None,
            last_trade_time=_maybe_datetime_seconds(row.get("last_traded_time")),
            exchange_timestamp=_maybe_datetime_seconds(row.get("exchange_timestamp")),
            ohlc_open=_maybe_positive_float(row.get("open_price")),
            ohlc_high=_maybe_positive_float(row.get("high_price")),
            ohlc_low=_maybe_positive_float(row.get("low_price")),
            ohlc_close=_maybe_positive_float(row.get("close_price")),
        )

        snap = make_snapshot(
            instrument_id=instrument_id,
            symbol=canonical_symbol,
            capture_time=capture_time,
            bids=bids,
            asks=asks,
            trade=trade,
            source="clickhouse",
            is_partial=False,
            is_stale=False,
            validate=True,
        )

        return snap
    except Exception:
        logger.exception("Failed to parse ClickHouse row into DepthSnapshot")
        return None


def _parse_depth_side(
    row: dict[str, Any], *, side: str
) -> list[DepthLevel]:
    if side not in {"bid", "ask"}:
        raise ValueError("side must be 'bid' or 'ask'")

    levels: list[DepthLevel] = []
    for i in range(5):
        price = _maybe_float(row.get(f"{side}_price_{i}"))
        size = _maybe_int(row.get(f"{side}_size_{i}"))
        orders = _maybe_int(row.get(f"{side}_orders_{i}"))

        # ClickHouse wide rows may include zeros for missing levels.
        if price is None or price <= 0:
            continue

        levels.append(
            DepthLevel(
                price=price,
                size=int(size or 0),
                orders=int(orders or 0),
            )
        )

    return levels


def _maybe_int(v: Any) -> int | None:
    if v is None:
        return None
    try:
        return int(v)
    except (TypeError, ValueError):
        return None


def _maybe_float(v: Any) -> float | None:
    if v is None:
        return None
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def _maybe_positive_float(v: Any) -> float | None:
    """Parse float, treating 0 or negative as None.
    
    ClickHouse stores 0 for missing price fields (average_traded_price, OHLC, etc.).
    The model validation requires these to be > 0 when present, so we convert 0 to None.
    """
    f = _maybe_float(v)
    if f is None or f <= 0:
        return None
    return f


def _maybe_datetime(v: Any) -> datetime | None:
    if v is None:
        return None
    if isinstance(v, datetime):
        return v
    return None


def _maybe_datetime_seconds(v: Any) -> datetime | None:
    dt = _maybe_datetime(v)
    if dt is None:
        return None
    # ClickHouse columns like exchange_timestamp / last_traded_time are treated
    # as second-precision timestamps for downstream logic.
    return dt.replace(microsecond=0)
