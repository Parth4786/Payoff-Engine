from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime


@dataclass(slots=True)
class GapState:
    last_capture_time: datetime | None = None


class GapDetector:
    """Marks snapshots stale when capture_time gaps exceed a threshold."""

    def __init__(self, *, gap_threshold_ms: int) -> None:
        if gap_threshold_ms <= 0:
            raise ValueError("gap_threshold_ms must be > 0")
        self._gap_threshold_ms = gap_threshold_ms
        self._state = GapState(last_capture_time=None)

    @property
    def last_capture_time(self) -> datetime | None:
        return self._state.last_capture_time

    def is_gap(self, capture_time: datetime) -> bool:
        prev = self._state.last_capture_time
        self._state.last_capture_time = capture_time
        if prev is None:
            return False
        dt_ms = (capture_time - prev).total_seconds() * 1000.0
        return dt_ms > float(self._gap_threshold_ms)
