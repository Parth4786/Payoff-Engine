from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime


@dataclass(slots=True)
class DedupState:
    last_capture_time: datetime | None = None
    duplicate_drops_total: int = 0
    reorder_drops_total: int = 0


class Deduplicator:
    """De-duplication + ordering gate for a single symbol.

    Policy (per docs/STREAMING_ENGINE.md):
    - If capture_time regresses: drop.
    - If capture_time is identical to last accepted: drop.
    - Else: accept.
    """

    def __init__(self) -> None:
        self._state = DedupState(last_capture_time=None)

    @property
    def last_capture_time(self) -> datetime | None:
        return self._state.last_capture_time

    @property
    def duplicate_drops_total(self) -> int:
        return int(self._state.duplicate_drops_total)

    @property
    def reorder_drops_total(self) -> int:
        return int(self._state.reorder_drops_total)

    def accept(self, capture_time: datetime) -> bool:
        prev = self._state.last_capture_time
        if prev is None:
            self._state.last_capture_time = capture_time
            return True

        if capture_time < prev:
            self._state.reorder_drops_total += 1
            return False

        if capture_time == prev:
            self._state.duplicate_drops_total += 1
            return False

        self._state.last_capture_time = capture_time
        return True
