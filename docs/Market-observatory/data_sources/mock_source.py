from __future__ import annotations

from collections.abc import AsyncIterator, Iterable
from dataclasses import dataclass
from datetime import datetime

from core.datasource import MarketDataSource
from core.models import DepthSnapshot


@dataclass(frozen=True, slots=True)
class MockDataSource(MarketDataSource):
    """Deterministic in-memory datasource for tests.

    - Stores per-symbol snapshot sequences.
    - Replay filters by time.
    - Streaming yields snapshots in-order then terminates.

    This is intentionally minimal; it exists to test parity/invariants in later steps.
    """

    _by_symbol: dict[str, tuple[DepthSnapshot, ...]]

    @staticmethod
    def from_snapshots(snapshots: Iterable[DepthSnapshot]) -> "MockDataSource":
        by_symbol: dict[str, list[DepthSnapshot]] = {}
        for s in snapshots:
            by_symbol.setdefault(s.symbol, []).append(s)

        normalized: dict[str, tuple[DepthSnapshot, ...]] = {}
        for symbol, seq in by_symbol.items():
            normalized[symbol] = tuple(sorted(seq, key=lambda x: x.capture_time))
        return MockDataSource(_by_symbol=normalized)

    async def get_symbols(self) -> list[str]:
        return sorted(self._by_symbol.keys())

    async def replay_snapshots(
        self,
        *,
        symbol: str,
        start_time: datetime,
        end_time: datetime,
        max_rows: int = 10000,
    ) -> list[DepthSnapshot]:
        seq = self._by_symbol.get(symbol, ())
        out = [s for s in seq if start_time <= s.capture_time <= end_time]
        return out[:max_rows]

    async def stream_snapshots(self, *, symbol: str) -> AsyncIterator[DepthSnapshot]:
        for s in self._by_symbol.get(symbol, ()):
            yield s
