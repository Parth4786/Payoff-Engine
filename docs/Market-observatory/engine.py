from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from math import log

from core.config import FeatureEnv
from core.instrument_manager import InstrumentManager
from core.models import DepthSnapshot


@dataclass(frozen=True, slots=True)
class FeatureSnapshot:
    instrument_id: int
    symbol: str
    capture_time: datetime
    source: str

    feature_valid: bool
    invalid_reasons: tuple[str, ...]

    midprice: float
    spread: float

    dt_capture_ms: float | None
    mid_change: float | None
    spread_change: float | None

    bid_depth: int
    ask_depth: int

    bid_depth_change: int | None
    ask_depth_change: int | None

    imbalance_l0: float | None
    imbalance_l5: float | None

    microprice: float | None

    last_price: float | None
    last_price_change: float | None

    traded_qty_total: int | None
    traded_qty_delta: int | None

    depth_slope: float | None
    lpi: float | None

    ofi: float | None

    shock_score: float | None
    liquidity_shock: bool


def compute_features(
    *,
    cfg: FeatureEnv,
    snapshot: DepthSnapshot,
    prev_snapshot: DepthSnapshot | None,
    tick_size: float | None = None,
) -> FeatureSnapshot:
    """Compute deterministic microstructure features from a snapshot.

    Purity rule:
    - This function is pure with respect to its inputs (snapshot, prev_snapshot, cfg).
    - Statefulness (rolling windows) belongs in the streaming service, not here.

    Performance:
    - Uses simple arithmetic only; no pandas/numpy.
    """

    reasons: list[str] = []

    # Data quality gates (hard degrade)
    if snapshot.is_stale:
        reasons.append("stale")
    if snapshot.is_partial:
        reasons.append("partial")

    mid = snapshot.midprice
    spread = snapshot.spread

    dt_capture_ms: float | None = None
    mid_change: float | None = None
    spread_change: float | None = None
    bid_depth_change: int | None = None
    ask_depth_change: int | None = None

    last_price = snapshot.trade.last_price
    last_price_change: float | None = None

    traded_qty_total = snapshot.trade.total_traded_quantity
    traded_qty_delta: int | None = None

    if prev_snapshot is not None and prev_snapshot.instrument_id == snapshot.instrument_id:
        dt_capture_ms = (snapshot.capture_time - prev_snapshot.capture_time).total_seconds() * 1000.0
        mid_change = mid - prev_snapshot.midprice
        spread_change = spread - prev_snapshot.spread

        if last_price is not None and prev_snapshot.trade.last_price is not None:
            last_price_change = float(last_price) - float(prev_snapshot.trade.last_price)

        if traded_qty_total is not None and prev_snapshot.trade.total_traded_quantity is not None:
            traded_qty_delta = int(traded_qty_total) - int(prev_snapshot.trade.total_traded_quantity)

    bid_depth = sum(int(l.size) for l in snapshot.bids)
    ask_depth = sum(int(l.size) for l in snapshot.asks)

    if prev_snapshot is not None and prev_snapshot.instrument_id == snapshot.instrument_id:
        prev_bid_depth = sum(int(l.size) for l in prev_snapshot.bids)
        prev_ask_depth = sum(int(l.size) for l in prev_snapshot.asks)
        bid_depth_change = bid_depth - prev_bid_depth
        ask_depth_change = ask_depth - prev_ask_depth

    imbalance_l0 = _imbalance(
        int(snapshot.best_bid.size),
        int(snapshot.best_ask.size),
    )
    imbalance_l5 = _imbalance(bid_depth, ask_depth)

    microprice: float | None = None
    if not snapshot.is_stale and not snapshot.is_partial:
        microprice = _microprice(
            bid_price=float(snapshot.best_bid.price),
            ask_price=float(snapshot.best_ask.price),
            bid_qty=int(snapshot.best_bid.size),
            ask_qty=int(snapshot.best_ask.size),
        )

    depth_slope: float | None = None
    lpi: float | None = None

    if bid_depth > 0 and ask_depth > 0:
        depth_slope = _depth_slope(snapshot, denom_eps=cfg.depth_slope_eps)

        if imbalance_l5 is not None:
            denom = max(spread, max((tick_size or 0.0), cfg.spread_floor))
            lpi = float(imbalance_l5) * log(1.0 + bid_depth + ask_depth) / denom

    ofi: float | None = None
    if prev_snapshot is not None:
        ofi = _snapshot_ofi(cfg=cfg, prev=prev_snapshot, cur=snapshot)

    shock_score: float | None = None
    liquidity_shock = False
    if prev_snapshot is not None:
        shock_score = _shock_score(prev_snapshot, snapshot)
        if shock_score is not None and shock_score > cfg.liquidity_shock_threshold:
            last_qty = snapshot.trade.last_qty
            if last_qty is None or last_qty == 0:
                liquidity_shock = True

    feature_valid = len(reasons) == 0

    return FeatureSnapshot(
        instrument_id=snapshot.instrument_id,
        symbol=snapshot.symbol,
        capture_time=snapshot.capture_time,
        source=snapshot.source,
        feature_valid=feature_valid,
        invalid_reasons=tuple(reasons),
        midprice=mid,
        spread=spread,
        dt_capture_ms=dt_capture_ms,
        mid_change=mid_change,
        spread_change=spread_change,
        bid_depth=bid_depth,
        ask_depth=ask_depth,
        bid_depth_change=bid_depth_change,
        ask_depth_change=ask_depth_change,
        imbalance_l0=imbalance_l0,
        imbalance_l5=imbalance_l5,
        microprice=microprice,
        last_price=last_price,
        last_price_change=last_price_change,
        traded_qty_total=traded_qty_total,
        traded_qty_delta=traded_qty_delta,
        depth_slope=depth_slope,
        lpi=lpi,
        ofi=ofi,
        shock_score=shock_score,
        liquidity_shock=liquidity_shock,
    )


def compute_features_with_instruments(
    *,
    cfg: FeatureEnv,
    snapshot: DepthSnapshot,
    prev_snapshot: DepthSnapshot | None,
    instruments: InstrumentManager,
) -> FeatureSnapshot:
    """Compute features and auto-enrich tick_size from instrument master.

    - `DepthSnapshot.instrument_id` must be the Kite `exchange_token`.
    - If metadata is missing, we compute with `tick_size=None` (still deterministic).
    """

    # IMPORTANT: prefer canonical symbol lookups to avoid cross-exchange exchange_token collisions.
    info = instruments.get_by_canonical_symbol(snapshot.symbol) or instruments.get(snapshot.instrument_id)
    tick_size = info.tick_size if info is not None else None
    return compute_features(cfg=cfg, snapshot=snapshot, prev_snapshot=prev_snapshot, tick_size=tick_size)


def _imbalance(bid_qty: int, ask_qty: int) -> float | None:
    denom = bid_qty + ask_qty
    if denom <= 0:
        return None
    return (bid_qty - ask_qty) / denom


def _microprice(*, bid_price: float, ask_price: float, bid_qty: int, ask_qty: int) -> float | None:
    denom = bid_qty + ask_qty
    if denom <= 0:
        return None
    return (ask_price * bid_qty + bid_price * ask_qty) / denom


def _depth_slope(snapshot: DepthSnapshot, *, denom_eps: float) -> float:
    mid = snapshot.midprice

    total = 0.0
    for lvl in snapshot.bids:
        denom = abs(float(lvl.price) - mid)
        total += float(lvl.size) / max(denom, denom_eps)

    for lvl in snapshot.asks:
        denom = abs(float(lvl.price) - mid)
        total += float(lvl.size) / max(denom, denom_eps)

    return total


def _approx_equal(a: float, b: float, *, eps: float) -> bool:
    return abs(a - b) <= eps


def _snapshot_ofi(*, cfg: FeatureEnv, prev: DepthSnapshot, cur: DepthSnapshot) -> float:
    # Level-0 size deltas only when price unchanged.
    delta_bid = 0.0
    if _approx_equal(float(prev.best_bid.price), float(cur.best_bid.price), eps=cfg.price_eps):
        delta_bid = float(cur.best_bid.size - prev.best_bid.size)

    delta_ask = 0.0
    if _approx_equal(float(prev.best_ask.price), float(cur.best_ask.price), eps=cfg.price_eps):
        delta_ask = float(cur.best_ask.size - prev.best_ask.size)

    # Aggressive proxy via last trade.
    aggressive_buy = 0.0
    aggressive_sell = 0.0

    last_price = cur.trade.last_price
    last_qty = cur.trade.last_qty
    if last_price is not None and last_qty is not None and last_qty > 0:
        if _approx_equal(float(last_price), float(cur.best_ask.price), eps=cfg.price_eps):
            aggressive_buy = float(last_qty)
        elif _approx_equal(float(last_price), float(cur.best_bid.price), eps=cfg.price_eps):
            aggressive_sell = float(last_qty)

    return delta_bid - delta_ask + aggressive_buy - aggressive_sell


def _shock_score(prev: DepthSnapshot, cur: DepthSnapshot) -> float | None:
    prev_bid_depth = sum(int(l.size) for l in prev.bids)
    prev_ask_depth = sum(int(l.size) for l in prev.asks)
    cur_bid_depth = sum(int(l.size) for l in cur.bids)
    cur_ask_depth = sum(int(l.size) for l in cur.asks)

    if prev_bid_depth <= 0 or prev_ask_depth <= 0:
        return None

    bid_term = (prev_bid_depth - cur_bid_depth) / prev_bid_depth
    ask_term = (prev_ask_depth - cur_ask_depth) / prev_ask_depth
    return bid_term + ask_term
