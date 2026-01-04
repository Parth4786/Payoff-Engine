from __future__ import annotations

from collections.abc import AsyncIterator
from datetime import datetime
from typing import Protocol

from core.models import DepthSnapshot


class MarketDataSource(Protocol):
    """Canonical market data source interface.

    All external feeds (ClickHouse, Kite WebSocket, mocks) must normalize data into
    `DepthSnapshot` and expose it through this interface.

    Non-negotiable:
    - No other module may query ClickHouse (or any feed) directly.
    - Replay and streaming must converge into the same downstream pipeline.
    """

    async def get_symbols(self) -> list[str]:
        """Return all supported symbols for this datasource."""

    async def replay_snapshots(
        self,
        *,
        symbol: str,
        start_time: datetime,
        end_time: datetime,
        max_rows: int = 10000,
    ) -> list[DepthSnapshot]:
        """Return snapshots within [start_time, end_time], ordered by capture_time."""

    async def stream_snapshots(self, *, symbol: str) -> AsyncIterator[DepthSnapshot]:
        """Yield a live stream of snapshots for `symbol`.

        The stream may be infinite. Implementations must be defensive against:
        duplicates, out-of-order updates, partial updates, and gaps.
        """
