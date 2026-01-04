from __future__ import annotations

import asyncio
import logging
from collections.abc import AsyncIterator
from dataclasses import dataclass
from datetime import datetime

from core.datasource import MarketDataSource
from core.models import DepthSnapshot

logger = logging.getLogger(__name__)


@dataclass(frozen=True, slots=True)
class FailoverConfig:
    # If the primary stream produces no snapshots for this duration, switch to secondary.
    primary_silence_ms: int


@dataclass(slots=True)
class FailoverDataSource(MarketDataSource):
    """Per-symbol failover wrapper.

    - Replay always comes from `primary` (ClickHouse).
    - Streaming prefers `primary`, but switches to `secondary` if the primary is silent.

    Failover selection happens before downstream processing; snapshots preserve their `source`.
    """

    primary: MarketDataSource
    secondary: MarketDataSource
    config: FailoverConfig

    async def get_symbols(self) -> list[str]:
        # Union; callers can still decide which ones are tradeable.
        a = await self.primary.get_symbols()
        b = await self.secondary.get_symbols()
        return sorted(set(a) | set(b), key=str)

    async def replay_snapshots(
        self,
        *,
        symbol: str,
        start_time: datetime,
        end_time: datetime,
        max_rows: int = 10000,
    ) -> list[DepthSnapshot]:
        return await self.primary.replay_snapshots(
            symbol=symbol,
            start_time=start_time,
            end_time=end_time,
            max_rows=max_rows,
        )

    async def stream_snapshots(self, *, symbol: str) -> AsyncIterator[DepthSnapshot]:
        if self.config.primary_silence_ms <= 0:
            raise ValueError("primary_silence_ms must be > 0")

        primary_q: asyncio.Queue[DepthSnapshot] = asyncio.Queue(maxsize=1000)
        secondary_q: asyncio.Queue[DepthSnapshot] = asyncio.Queue(maxsize=1000)

        async def _pump(ds: MarketDataSource, q: asyncio.Queue[DepthSnapshot]) -> None:
            async for snap in ds.stream_snapshots(symbol=symbol):
                await q.put(snap)

        primary_task = asyncio.create_task(_pump(self.primary, primary_q))
        secondary_task = asyncio.create_task(_pump(self.secondary, secondary_q))

        using_secondary = False
        silence_sec = self.config.primary_silence_ms / 1000.0

        try:
            while True:
                if not using_secondary:
                    try:
                        snap = await asyncio.wait_for(primary_q.get(), timeout=silence_sec)
                        yield snap
                        continue
                    except asyncio.TimeoutError:
                        using_secondary = True
                        logger.warning("Failover: switching to secondary", extra={"symbol": symbol})
                        continue

                # Using secondary: yield secondary ticks, but immediately switch back if primary recovers.
                primary_get = asyncio.create_task(primary_q.get())
                secondary_get = asyncio.create_task(secondary_q.get())
                done, pending = await asyncio.wait(
                    {primary_get, secondary_get},
                    return_when=asyncio.FIRST_COMPLETED,
                )

                for p in pending:
                    p.cancel()

                if primary_get in done:
                    using_secondary = False
                    snap = primary_get.result()
                    logger.warning("Failover: primary recovered; switching back", extra={"symbol": symbol})
                    yield snap
                else:
                    snap = secondary_get.result()
                    yield snap
        finally:
            for t in (primary_task, secondary_task):
                t.cancel()
            await asyncio.gather(primary_task, secondary_task, return_exceptions=True)
