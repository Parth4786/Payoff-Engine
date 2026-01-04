from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from math import sqrt

from core.config import ExecutionEnv
from features.engine import FeatureSnapshot

from execution.models import ExecutionDecision, ExecutionHint


@dataclass(slots=True)
class _RollingStats:
    window: int
    values: deque[float]
    sum_x: float = 0.0
    sum_x2: float = 0.0

    @staticmethod
    def create(window: int) -> "_RollingStats":
        return _RollingStats(window=window, values=deque(maxlen=window))

    def add(self, x: float) -> None:
        if len(self.values) == self.values.maxlen:
            old = self.values[0]
            self.sum_x -= old
            self.sum_x2 -= old * old
        self.values.append(x)
        self.sum_x += x
        self.sum_x2 += x * x

    def std(self) -> float | None:
        n = len(self.values)
        if n < 2:
            return None
        mean = self.sum_x / float(n)
        var = max(0.0, (self.sum_x2 / float(n)) - (mean * mean))
        return sqrt(var)


def _percentile(values: list[float], pct: float) -> float | None:
    if not values:
        return None
    if pct <= 0:
        return min(values)
    if pct >= 1:
        return max(values)

    xs = sorted(values)
    k = (len(xs) - 1) * pct
    f = int(k)
    c = min(f + 1, len(xs) - 1)
    if f == c:
        return xs[f]
    d = k - f
    return xs[f] * (1.0 - d) + xs[c] * d


class ExecutionHintEngine:
    """Desk-grade v2 execution hint engine.

    - Stateful (rolling stats) but deterministic given the snapshot sequence.
    - Output is explainable: hint + reason codes.

    IMPORTANT:
    - The frontend must never compute this.
    - Downstream must always be safe: WAIT is always valid.
    """

    def __init__(self, *, cfg: ExecutionEnv) -> None:
        self._cfg = cfg
        self._depth_slope_hist: deque[float] = deque(maxlen=int(cfg.rolling_window))
        self._ofi_stats = _RollingStats.create(window=int(cfg.rolling_window))

    def decide(
        self,
        *,
        feature: FeatureSnapshot,
        tick_size: float | None,
        kill_switch_active: bool,
    ) -> ExecutionDecision:
        reasons: list[str] = []

        if kill_switch_active:
            return ExecutionDecision(hint=ExecutionHint.WAIT, reasons=("kill_switch",))

        if not feature.feature_valid:
            return ExecutionDecision(hint=ExecutionHint.WAIT, reasons=("feature_invalid",))

        if tick_size is None or tick_size <= 0:
            return ExecutionDecision(hint=ExecutionHint.WAIT, reasons=("missing_tick_size",))

        spread_tight = float(self._cfg.spread_tight_ticks) * float(tick_size)
        spread_max = float(self._cfg.spread_max_ticks) * float(tick_size)

        if feature.spread <= 0:
            return ExecutionDecision(hint=ExecutionHint.WAIT, reasons=("spread_invalid",))

        if feature.spread > spread_max:
            return ExecutionDecision(hint=ExecutionHint.WAIT, reasons=("spread_wide",))

        # Update rolling state (after hard safety checks so we don't poison history too early).
        if feature.depth_slope is not None:
            self._depth_slope_hist.append(float(feature.depth_slope))
        self._ofi_stats.add(float(feature.ofi or 0.0))

        if len(self._depth_slope_hist) < int(self._cfg.min_history):
            return ExecutionDecision(hint=ExecutionHint.WAIT, reasons=("insufficient_history",))

        depth_vals = list(self._depth_slope_hist)
        depth_strong = _percentile(depth_vals, float(self._cfg.depth_strong_pct))
        depth_min = _percentile(depth_vals, float(self._cfg.depth_min_pct))

        if depth_strong is None or depth_min is None or feature.depth_slope is None:
            return ExecutionDecision(hint=ExecutionHint.WAIT, reasons=("insufficient_history",))

        ofi_std = self._ofi_stats.std()
        ofi_spike = None if ofi_std is None else float(ofi_std) * float(self._cfg.ofi_spike_std_mult)

        imb_l0 = feature.imbalance_l0
        imb_l5 = feature.imbalance_l5
        abs_imb_l0 = abs(float(imb_l0)) if imb_l0 is not None else None
        abs_imb_l5 = abs(float(imb_l5)) if imb_l5 is not None else None

        # PASSIVE
        if (
            float(feature.depth_slope) >= float(depth_strong)
            and feature.spread <= spread_tight
            and abs_imb_l5 is not None
            and abs_imb_l5 <= float(self._cfg.imbalance_neutral)
            and (ofi_spike is None or abs(float(feature.ofi or 0.0)) < ofi_spike)
        ):
            reasons.append("thick_book")
            reasons.append("tight_spread")
            reasons.append("neutral_imbalance")
            return ExecutionDecision(hint=ExecutionHint.PASSIVE, reasons=tuple(reasons))

        # AGGRESSIVE
        pressure = False
        if abs_imb_l0 is not None and abs_imb_l0 >= float(self._cfg.imbalance_strong):
            pressure = True
            reasons.append("l0_pressure")

        if ofi_spike is not None and abs(float(feature.ofi or 0.0)) >= ofi_spike:
            pressure = True
            reasons.append("ofi_spike")

        # Microprice drift proxy: when present and far from mid.
        if feature.microprice is not None:
            if abs(float(feature.microprice) - float(feature.midprice)) >= float(tick_size):
                pressure = True
                reasons.append("microprice_drift")

        if feature.spread <= spread_tight and float(feature.depth_slope) >= float(depth_min) and pressure:
            reasons.append("tight_spread")
            return ExecutionDecision(hint=ExecutionHint.AGGRESSIVE, reasons=tuple(reasons))

        return ExecutionDecision(hint=ExecutionHint.WAIT, reasons=("default_wait",))
