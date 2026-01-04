"""Rolling-window feature tracker for streaming and replay.

This module computes stateful rolling-window features that require
more than just (prev, cur) snapshots:
- tape_speed: ticks per second (rolling window)
- volume_rate: traded quantity per second
- absorption_flag: high volume + flat price
- spread_percentile: current spread vs recent history

These are computed server-side to maintain the "frontend never computes" rule.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from datetime import datetime


@dataclass(frozen=True, slots=True)
class RollingFeatures:
    """Rolling-window derived features.

    All fields are nullable when insufficient history exists.
    """

    # Ticks per second (based on recent tick arrival rate)
    tape_speed: float | None

    # Traded volume per second
    volume_rate: float | None

    # Absorption: high volume with minimal price movement
    absorption_flag: bool

    # Spread percentile vs rolling window (0-100)
    spread_percentile: float | None

    # Price velocity (mid change per second)
    price_velocity: float | None

    # Pressure (derived from snapshot OFI) as a per-second rate.
    # Positive = buy pressure, negative = sell pressure.
    pressure_rate: float | None

    # Rolling pressure summaries keyed by window length (seconds).
    #
    # - pressure_net: accumulated OFI over the window (time-integral; robust for screening).
    # - pressure_mean: average pressure rate over the observed window duration (stable for charts).
    pressure_net: dict[int, float | None]
    pressure_mean: dict[int, float | None]


@dataclass(frozen=True, slots=True)
class RollingConfig:
    """Configuration for rolling window calculations."""

    # Window size in ticks
    window_ticks: int = 50

    # Minimum ticks required before computing features
    min_ticks: int = 10

    # Absorption thresholds
    absorption_volume_percentile: float = 0.8  # Volume > 80th percentile
    absorption_price_move_max: float = 0.0005  # Max price move ratio for absorption

    # Spread percentile uses full window

    # Pressure windows (time-based) derived from OFI.
    pressure_windows_sec: tuple[int, ...] = (5, 10, 30)
    pressure_dt_floor_ms: float = 50.0
    pressure_min_fill_ratio: float = 0.5


@dataclass(slots=True)
class _TickRecord:
    """Single tick record for rolling window."""

    capture_time: datetime
    spread: float
    midprice: float
    traded_qty_delta: int | None
    mid_change: float | None


@dataclass(slots=True)
class _PressureRecord:
    end_time: datetime
    dt_used_sec: float
    ofi: float


class RollingWindowTracker:
    """Per-symbol rolling window tracker.

    This is stateful and must be instantiated per symbol.
    Deterministic: same tick sequence → same features.
    """

    def __init__(self, cfg: RollingConfig | None = None) -> None:
        self._cfg = cfg if cfg is not None else RollingConfig()
        self._ticks: deque[_TickRecord] = deque(maxlen=self._cfg.window_ticks)

        # Pressure is time-windowed, not tick-windowed.
        self._prev_capture_time: datetime | None = None
        self._pressure: deque[_PressureRecord] = deque()

    def update(
        self,
        *,
        capture_time: datetime,
        spread: float,
        midprice: float,
        traded_qty_delta: int | None,
        mid_change: float | None,
        ofi: float | None = None,
    ) -> RollingFeatures:
        """Add a tick and compute rolling features."""

        record = _TickRecord(
            capture_time=capture_time,
            spread=spread,
            midprice=midprice,
            traded_qty_delta=traded_qty_delta,
            mid_change=mid_change,
        )
        self._ticks.append(record)

        self._update_pressure(capture_time=capture_time, ofi=ofi)

        return self._compute(capture_time=capture_time)

    def _compute(self, *, capture_time: datetime) -> RollingFeatures:
        n = len(self._ticks)
        min_ticks = int(self._cfg.min_ticks)

        pressure_rate, pressure_net, pressure_mean = self._compute_pressure(capture_time=capture_time)

        if n < min_ticks:
            return RollingFeatures(
                tape_speed=None,
                volume_rate=None,
                absorption_flag=False,
                spread_percentile=None,
                price_velocity=None,
                pressure_rate=pressure_rate,
                pressure_net=pressure_net,
                pressure_mean=pressure_mean,
            )

        ticks = list(self._ticks)

        # Time span
        t_first = ticks[0].capture_time
        t_last = ticks[-1].capture_time
        span_sec = (t_last - t_first).total_seconds()

        # Tape speed: ticks per second
        tape_speed: float | None = None
        if span_sec > 0:
            tape_speed = float(n - 1) / span_sec

        # Volume rate: total traded qty delta / time span
        volume_rate: float | None = None
        volumes = [t.traded_qty_delta for t in ticks if t.traded_qty_delta is not None]
        if volumes and span_sec > 0:
            total_vol = sum(abs(v) for v in volumes)
            volume_rate = float(total_vol) / span_sec

        # Spread percentile
        spreads = sorted([t.spread for t in ticks])
        current_spread = ticks[-1].spread
        spread_percentile = _percentile_rank(spreads, current_spread)

        # Price velocity: total mid change / time span
        price_velocity: float | None = None
        if span_sec > 0:
            first_mid = ticks[0].midprice
            last_mid = ticks[-1].midprice
            price_velocity = (last_mid - first_mid) / span_sec

        # Absorption detection
        absorption_flag = self._detect_absorption(
            ticks=ticks,
            volume_rate=volume_rate,
            price_velocity=price_velocity,
        )

        return RollingFeatures(
            tape_speed=tape_speed,
            volume_rate=volume_rate,
            absorption_flag=absorption_flag,
            spread_percentile=spread_percentile,
            price_velocity=price_velocity,
            pressure_rate=pressure_rate,
            pressure_net=pressure_net,
            pressure_mean=pressure_mean,
        )

    def _update_pressure(self, *, capture_time: datetime, ofi: float | None) -> None:
        # Pressure needs a time delta; also skip missing OFI.
        prev = self._prev_capture_time
        self._prev_capture_time = capture_time

        if prev is None:
            return

        dt_sec = (capture_time - prev).total_seconds()
        if dt_sec <= 0:
            return

        if ofi is None:
            return

        dt_floor_sec = max(0.0, float(self._cfg.pressure_dt_floor_ms) / 1000.0)
        dt_used = max(dt_sec, dt_floor_sec)

        # Use dt_used both for rate and for averaging to avoid burst spikes.
        self._pressure.append(
            _PressureRecord(
                end_time=capture_time,
                dt_used_sec=dt_used,
                ofi=float(ofi),
            )
        )

        # Evict records older than the maximum configured window.
        windows = self._cfg.pressure_windows_sec
        if not windows:
            return
        max_window = max(int(w) for w in windows)
        if max_window <= 0:
            return

        cutoff = capture_time.timestamp() - float(max_window)
        while self._pressure and self._pressure[0].end_time.timestamp() < cutoff:
            self._pressure.popleft()

        # Hard safety cap (prevents unbounded growth on pathological timestamps).
        if len(self._pressure) > 5000:
            while len(self._pressure) > 5000:
                self._pressure.popleft()

    def _compute_pressure(
        self, *, capture_time: datetime
    ) -> tuple[float | None, dict[int, float | None], dict[int, float | None]]:
        windows = tuple(int(w) for w in self._cfg.pressure_windows_sec)
        windows = tuple(sorted(set(w for w in windows if w > 0)))

        # Always return keys for determinism.
        pressure_net: dict[int, float | None] = {w: None for w in windows}
        pressure_mean: dict[int, float | None] = {w: None for w in windows}

        if not windows or not self._pressure:
            return None, pressure_net, pressure_mean

        # Instant rate from the most recent record.
        last = self._pressure[-1]
        rate = None
        if last.dt_used_sec > 0:
            rate = float(last.ofi) / float(last.dt_used_sec)

        min_fill = float(self._cfg.pressure_min_fill_ratio)

        # Compute each window by scanning backward until cutoff.
        for w in windows:
            cutoff_ts = capture_time.timestamp() - float(w)
            sum_ofi = 0.0
            sum_dt = 0.0
            for rec in reversed(self._pressure):
                if rec.end_time.timestamp() < cutoff_ts:
                    break
                sum_ofi += float(rec.ofi)
                sum_dt += float(rec.dt_used_sec)

            if sum_dt <= 0:
                continue

            if min_fill > 0 and sum_dt < (float(w) * min_fill):
                continue

            pressure_net[w] = float(sum_ofi)
            pressure_mean[w] = float(sum_ofi) / float(sum_dt)

        return rate, pressure_net, pressure_mean

    def _detect_absorption(
        self,
        *,
        ticks: list[_TickRecord],
        volume_rate: float | None,
        price_velocity: float | None,
    ) -> bool:
        """Detect absorption: high volume + flat price.

        Absorption indicates large orders being absorbed without moving price.
        """

        if volume_rate is None or price_velocity is None:
            return False

        if volume_rate <= 0:
            return False

        # Get recent volume rates for percentile comparison
        volumes = [t.traded_qty_delta for t in ticks if t.traded_qty_delta is not None]
        if len(volumes) < int(self._cfg.min_ticks):
            return False

        abs_volumes = sorted([abs(v) for v in volumes])
        current_vol = abs(ticks[-1].traded_qty_delta or 0)

        # Volume must be above threshold percentile
        vol_pct = _percentile_rank(abs_volumes, float(current_vol))
        if vol_pct < self._cfg.absorption_volume_percentile * 100:
            return False

        # Price must be relatively flat
        first_mid = ticks[0].midprice
        if first_mid <= 0:
            return False

        last_mid = ticks[-1].midprice
        price_move_ratio = abs(last_mid - first_mid) / first_mid

        if price_move_ratio > self._cfg.absorption_price_move_max:
            return False

        return True


def _percentile_rank(sorted_values: list[float], value: float) -> float:
    """Compute percentile rank (0-100) of value in sorted list."""

    if not sorted_values:
        return 50.0

    n = len(sorted_values)
    if n == 1:
        return 50.0

    # Count values less than or equal
    count_le = 0
    for v in sorted_values:
        if v <= value:
            count_le += 1
        else:
            break

    return (count_le / n) * 100.0
