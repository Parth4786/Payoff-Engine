from __future__ import annotations

import asyncio
import logging
from collections.abc import AsyncIterator
from dataclasses import dataclass
from datetime import datetime, timezone
import os
from typing import Any, Protocol

from core.datasource import MarketDataSource
from core.instrument_manager import InstrumentManager
from core.models import DepthLevel, DepthSnapshot, TradeInfo, make_snapshot
from data_sources.clickhouse_source import parse_canonical_symbol, make_canonical_symbol

logger = logging.getLogger(__name__)


class KiteTickerLike(Protocol):
    def connect(self, threaded: bool = True) -> None:  # pragma: no cover
        ...

    def close(self) -> None:  # pragma: no cover
        ...

    def subscribe(self, tokens: list[int]) -> None:  # pragma: no cover
        ...

    def unsubscribe(self, tokens: list[int]) -> None:  # pragma: no cover
        ...

    def set_mode(self, mode: str, tokens: list[int]) -> None:  # pragma: no cover
        ...


class KiteTickerFactory(Protocol):
    def __call__(self, api_key: str, access_token: str) -> KiteTickerLike:  # pragma: no cover
        ...


@dataclass(frozen=True, slots=True)
class KiteWSConfig:
    api_key: str
    access_token: str
    ws_mode: str


@dataclass(slots=True)
class KiteWebSocketDataSource(MarketDataSource):
    """Kite WS datasource for live failover.

    This is best-effort and defensive:
    - If instrument_token -> exchange_token mapping is missing, drop the tick (never guess).
    - If depth is missing (not FULL mode), drop the tick (DepthSnapshot requires bids+asks).

    Replay is not supported (ClickHouse is the replay source).
    """

    config: KiteWSConfig
    instruments: InstrumentManager
    ticker_factory: KiteTickerFactory

    async def get_symbols(self) -> list[str]:
        # Return canonical symbols (exchange:exchange_token) sorted
        symbols = sorted(self.instruments.by_canonical_symbol.keys())
        return symbols

    async def replay_snapshots(
        self,
        *,
        symbol: str,
        start_time: datetime,
        end_time: datetime,
        max_rows: int = 10000,
    ):
        return []

    async def stream_snapshots(self, *, symbol: str) -> AsyncIterator[DepthSnapshot]:
        # Parse canonical symbol (exchange:exchange_token)
        exchange, exchange_token = parse_canonical_symbol(symbol)
        info = self.instruments.get_by_canonical_symbol(symbol)
        if info is None:
            raise ValueError(f"Unknown canonical symbol for Kite WS: {symbol}")

        instrument_token = info.instrument_token
        canonical_symbol = symbol  # Keep for filtering and snapshot creation

        log_ticks = os.getenv("KITE_WS_LOG_TICKS", "0").strip() in {"1", "true", "yes", "on"}

        queue: asyncio.Queue[DepthSnapshot] = asyncio.Queue(maxsize=1000)
        stop = asyncio.Event()
        got_first_tick = False
        got_first_raw_tick = False

        # kiteconnect calls `on_ticks(ws, ticks)`.
        # Be defensive: some mocks/tests may call `on_ticks(ticks)`.
        def _on_ticks(*args: Any):
            nonlocal got_first_tick, got_first_raw_tick

            ticks: Any
            if len(args) == 1:
                ticks = args[0]
            elif len(args) >= 2:
                ticks = args[1]
            else:
                return

            if not isinstance(ticks, list):
                return

            for t in ticks:
                if not isinstance(t, dict):
                    continue

                if not got_first_raw_tick:
                    got_first_raw_tick = True
                    has_depth = isinstance(t.get("depth"), dict)
                    logger.info(
                        "Kite WS raw tick received symbol=%s instrument_token=%s has_depth=%s mode=%s",
                        canonical_symbol,
                        instrument_token,
                        has_depth,
                        self.config.ws_mode,
                    )

                snap = _tick_to_snapshot(t, instruments=self.instruments)
                if snap is None:
                    continue
                # Filter by canonical symbol
                if snap.symbol != canonical_symbol:
                    continue
                if not got_first_tick:
                    got_first_tick = True
                    logger.info(
                        "Kite WS first depth snapshot symbol=%s instrument_token=%s exchange_token=%s capture_time=%s",
                        canonical_symbol,
                        instrument_token,
                        info.exchange_token,
                        snap.capture_time.isoformat(),
                    )
                elif log_ticks:
                    logger.info(
                        "Kite WS tick symbol=%s instrument_token=%s capture_time=%s",
                        canonical_symbol,
                        instrument_token,
                        snap.capture_time.isoformat(),
                    )
                try:
                    queue.put_nowait(snap)
                except asyncio.QueueFull:
                    logger.warning("Kite tick queue full; dropping", extra={"symbol": canonical_symbol})

        def _on_connect():
            try:
                logger.info(
                    "Kite WS subscribing symbol=%s instrument_token=%s exchange_token=%s mode=%s",
                    canonical_symbol,
                    instrument_token,
                    info.exchange_token,
                    self.config.ws_mode,
                )
                ticker.subscribe([instrument_token])
                ticker.set_mode(self.config.ws_mode, [instrument_token])
                logger.info(
                    "Kite WS subscribed symbol=%s instrument_token=%s mode=%s",
                    canonical_symbol,
                    instrument_token,
                    self.config.ws_mode,
                )
            except Exception:
                logger.exception("Kite WS subscribe failed")

        def _on_close():
            logger.info(
                "Kite WS closed",
                extra={
                    "symbol": canonical_symbol,
                    "instrument_token": instrument_token,
                },
            )
            stop.set()

        ticker = self.ticker_factory(self.config.api_key, self.config.access_token)

        # The kiteconnect KiteTicker sets attributes/callbacks; we do this dynamically to remain testable.
        setattr(ticker, "on_ticks", _on_ticks)
        setattr(ticker, "on_connect", lambda ws, resp=None: _on_connect())
        setattr(ticker, "on_close", lambda ws, code=None, reason=None: _on_close())

        ticker.connect(threaded=True)

        logger.info(
            "Kite WS stream started symbol=%s instrument_token=%s exchange_token=%s",
            canonical_symbol,
            instrument_token,
            info.exchange_token,
        )

        try:
            while not stop.is_set():
                snap = await queue.get()
                yield snap
        finally:
            try:
                ticker.unsubscribe([instrument_token])
            except Exception:
                pass
            try:
                ticker.close()
            except Exception:
                pass

            logger.info(
                "Kite WS stream stopped",
                extra={
                    "symbol": canonical_symbol,
                    "instrument_token": instrument_token,
                },
            )


def _tick_to_snapshot(tick: dict[str, Any], *, instruments: InstrumentManager) -> DepthSnapshot | None:
    instrument_token = tick.get("instrument_token")
    if instrument_token is None:
        return None

    try:
        instrument_token_int = int(instrument_token)
    except Exception:
        return None

    # Get instrument info to construct canonical symbol
    info = instruments.get_by_instrument_token(instrument_token_int)
    if info is None:
        logger.warning("Missing instrument_token mapping; dropping tick", extra={"instrument_token": instrument_token_int})
        return None

    exchange_token = info.exchange_token
    canonical_symbol = info.canonical_symbol

    depth = tick.get("depth")
    if not isinstance(depth, dict):
        return None

    bids_raw = depth.get("buy")
    asks_raw = depth.get("sell")
    if not isinstance(bids_raw, list) or not isinstance(asks_raw, list) or not bids_raw or not asks_raw:
        return None

    bids = _parse_depth_levels(bids_raw, side="buy")
    asks = _parse_depth_levels(asks_raw, side="sell")
    if not bids or not asks:
        return None

    capture_time = datetime.now(timezone.utc)

    trade = TradeInfo(
        last_price=_maybe_float(tick.get("last_price")),
        last_qty=_maybe_int(tick.get("last_quantity")),
        total_traded_quantity=_maybe_int(tick.get("volume")),
        average_traded_price=_maybe_float(tick.get("average_traded_price")),
        total_buy_quantity=_maybe_int(tick.get("buy_quantity")),
        total_sell_quantity=_maybe_int(tick.get("sell_quantity")),
        oi=_maybe_int(tick.get("oi")),
        oi_day_high=_maybe_int(tick.get("oi_day_high")),
        oi_day_low=_maybe_int(tick.get("oi_day_low")),
        last_trade_time=_maybe_ts_seconds(tick.get("last_trade_time")),
        exchange_timestamp=_maybe_ts_seconds(tick.get("exchange_timestamp")),
        ohlc_open=_maybe_float(_maybe_dict(tick.get("ohlc"), "open")),
        ohlc_high=_maybe_float(_maybe_dict(tick.get("ohlc"), "high")),
        ohlc_low=_maybe_float(_maybe_dict(tick.get("ohlc"), "low")),
        ohlc_close=_maybe_float(_maybe_dict(tick.get("ohlc"), "close")),
    )

    try:
        return make_snapshot(
            instrument_id=exchange_token,
            symbol=canonical_symbol,
            capture_time=capture_time,
            bids=bids,
            asks=asks,
            trade=trade,
            source="kite_ws",
            is_partial=False,
            is_stale=False,
            validate=True,
        )
    except Exception:
        logger.exception("Failed to normalize kite tick")
        return None


def _parse_depth_levels(rows: list[dict[str, Any]], *, side: str) -> list[DepthLevel]:
    levels: list[DepthLevel] = []
    for r in rows[:5]:
        price = _maybe_float(r.get("price"))
        qty = _maybe_int(r.get("quantity"))
        orders = _maybe_int(r.get("orders"))
        if price is None or price <= 0:
            continue
        levels.append(DepthLevel(price=float(price), size=int(qty or 0), orders=int(orders or 0)))

    # Kite buy side should be descending; sell ascending. The make_snapshot validation will enforce.
    return levels


def _maybe_int(v: Any) -> int | None:
    if v is None:
        return None
    try:
        return int(v)
    except Exception:
        return None


def _maybe_float(v: Any) -> float | None:
    if v is None:
        return None
    try:
        return float(v)
    except Exception:
        return None


def _maybe_dict(d: Any, key: str) -> Any:
    if not isinstance(d, dict):
        return None
    return d.get(key)


def _maybe_ts_seconds(v: Any) -> datetime | None:
    if v is None:
        return None
    if isinstance(v, datetime):
        return v.replace(microsecond=0)
    # KiteTicker usually supplies datetime, but handle raw epoch seconds too.
    try:
        ts = int(v)
        return datetime.fromtimestamp(ts, tz=timezone.utc).replace(microsecond=0)
    except Exception:
        return None
