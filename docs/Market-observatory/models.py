from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from typing import Literal, Sequence


Source = Literal["clickhouse", "kite_ws", "mock"]


class InvalidSnapshotError(ValueError):
    """Raised when a snapshot violates canonical structural invariants."""


@dataclass(frozen=True, slots=True)
class DepthLevel:
    price: float
    size: int
    orders: int

    def validate(self) -> None:
        if self.price <= 0:
            raise ValueError("DepthLevel.price must be > 0")
        if self.size < 0:
            raise ValueError("DepthLevel.size must be >= 0")
        if self.orders < 0:
            raise ValueError("DepthLevel.orders must be >= 0")


@dataclass(frozen=True, slots=True)
class TradeInfo:
    last_price: float | None
    last_qty: int | None

    # ClickHouse: total_traded_quantity; Kite: volume traded for the day.
    total_traded_quantity: int | None

    # ClickHouse: average_traded_price; Kite: average traded price.
    average_traded_price: float | None

    total_buy_quantity: int | None
    total_sell_quantity: int | None

    # Kite FULL mode includes OI fields (may be absent for some instruments).
    oi: int | None
    oi_day_high: int | None
    oi_day_low: int | None

    last_trade_time: datetime | None
    exchange_timestamp: datetime | None

    # Optional daily OHLC context.
    ohlc_open: float | None
    ohlc_high: float | None
    ohlc_low: float | None
    ohlc_close: float | None

    def validate(self) -> None:
        if self.last_price is not None and self.last_price <= 0:
            raise ValueError("TradeInfo.last_price must be > 0 when present")
        if self.last_qty is not None and self.last_qty < 0:
            raise ValueError("TradeInfo.last_qty must be >= 0 when present")
        if self.total_traded_quantity is not None and self.total_traded_quantity < 0:
            raise ValueError("TradeInfo.total_traded_quantity must be >= 0 when present")
        if self.average_traded_price is not None and self.average_traded_price <= 0:
            raise ValueError("TradeInfo.average_traded_price must be > 0 when present")
        if self.total_buy_quantity is not None and self.total_buy_quantity < 0:
            raise ValueError("TradeInfo.total_buy_quantity must be >= 0 when present")
        if self.total_sell_quantity is not None and self.total_sell_quantity < 0:
            raise ValueError("TradeInfo.total_sell_quantity must be >= 0 when present")
        if self.oi is not None and self.oi < 0:
            raise ValueError("TradeInfo.oi must be >= 0 when present")
        if self.oi_day_high is not None and self.oi_day_high < 0:
            raise ValueError("TradeInfo.oi_day_high must be >= 0 when present")
        if self.oi_day_low is not None and self.oi_day_low < 0:
            raise ValueError("TradeInfo.oi_day_low must be >= 0 when present")
        if self.ohlc_open is not None and self.ohlc_open <= 0:
            raise ValueError("TradeInfo.ohlc_open must be > 0 when present")
        if self.ohlc_high is not None and self.ohlc_high <= 0:
            raise ValueError("TradeInfo.ohlc_high must be > 0 when present")
        if self.ohlc_low is not None and self.ohlc_low <= 0:
            raise ValueError("TradeInfo.ohlc_low must be > 0 when present")
        if self.ohlc_close is not None and self.ohlc_close <= 0:
            raise ValueError("TradeInfo.ohlc_close must be > 0 when present")


@dataclass(frozen=True, slots=True)
class DepthSnapshot:
    # Canonical identity (must match ClickHouse instrument_id == Kite exchange_token).
    instrument_id: int
    symbol: str
    capture_time: datetime

    bids: tuple[DepthLevel, ...]
    asks: tuple[DepthLevel, ...]

    trade: TradeInfo

    source: Source
    is_partial: bool
    is_stale: bool

    def validate(self) -> None:
        if self.instrument_id <= 0:
            raise InvalidSnapshotError("DepthSnapshot.instrument_id must be > 0")
        if not self.symbol:
            raise InvalidSnapshotError("DepthSnapshot.symbol must be non-empty")
        if self.capture_time.tzinfo is None:
            # We allow naive datetimes as long as the system is consistent; enforce explicitness later.
            pass

        if len(self.bids) < 1:
            raise InvalidSnapshotError("DepthSnapshot.bids must contain at least 1 level")
        if len(self.asks) < 1:
            raise InvalidSnapshotError("DepthSnapshot.asks must contain at least 1 level")

        for level in self.bids:
            level.validate()
        for level in self.asks:
            level.validate()
        self.trade.validate()

        best_bid = self.bids[0].price
        best_ask = self.asks[0].price
        if best_bid >= best_ask:
            raise InvalidSnapshotError(
                "Top of book must satisfy best_bid < best_ask"
            )

        _validate_sorted_strict(self.bids, direction="desc", side="bids")
        _validate_sorted_strict(self.asks, direction="asc", side="asks")

    @property
    def best_bid(self) -> DepthLevel:
        return self.bids[0]

    @property
    def best_ask(self) -> DepthLevel:
        return self.asks[0]

    @property
    def midprice(self) -> float:
        return (self.best_bid.price + self.best_ask.price) / 2.0

    @property
    def spread(self) -> float:
        return self.best_ask.price - self.best_bid.price


def make_snapshot(
    *,
    instrument_id: int,
    symbol: str,
    capture_time: datetime,
    bids: Sequence[DepthLevel],
    asks: Sequence[DepthLevel],
    trade: TradeInfo,
    source: Source,
    is_partial: bool,
    is_stale: bool,
    validate: bool = True,
) -> DepthSnapshot:
    """Create a canonical `DepthSnapshot` from sequences.

    Notes:
    - Converts bids/asks into tuples for immutability.
    - Validation is on by default and enforces the data contract invariants.
    """

    snapshot = DepthSnapshot(
        instrument_id=instrument_id,
        symbol=symbol,
        capture_time=capture_time,
        bids=tuple(bids),
        asks=tuple(asks),
        trade=trade,
        source=source,
        is_partial=is_partial,
        is_stale=is_stale,
    )
    if validate:
        snapshot.validate()
    return snapshot


def _validate_sorted_strict(
    levels: Sequence[DepthLevel],
    *,
    direction: Literal["asc", "desc"],
    side: Literal["bids", "asks"],
) -> None:
    prices = [lvl.price for lvl in levels]

    if direction == "desc":
        is_ok = all(prices[i] > prices[i + 1] for i in range(len(prices) - 1))
        if not is_ok:
            raise InvalidSnapshotError(f"{side} must be strictly descending by price")
        return

    if direction == "asc":
        is_ok = all(prices[i] < prices[i + 1] for i in range(len(prices) - 1))
        if not is_ok:
            raise InvalidSnapshotError(f"{side} must be strictly ascending by price")
        return

    raise ValueError("direction must be 'asc' or 'desc'")
