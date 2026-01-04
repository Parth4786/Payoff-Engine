from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass
from typing import Any

from core.config import ExecutionEnv, FeatureEnv, KillSwitchEnv, RollingEnv, StreamingEnv, TradeabilityEnv
from core.instrument_manager import InstrumentManager
from core.models import DepthSnapshot
from core.payloads import SnapshotFeaturePayload

from streaming.pipeline import stream_feature_payloads, stream_processed_snapshots

logger = logging.getLogger(__name__)


@dataclass(slots=True)
class _SymbolEntry:
    task: asyncio.Task[None] | None
    subscribers: set[asyncio.Queue[tuple[str, SnapshotFeaturePayload]]]
    last_payload: SnapshotFeaturePayload | None


@dataclass(slots=True)
class _SnapshotEntry:
    task: asyncio.Task[None] | None
    subscribers: set[asyncio.Queue[tuple[str, DepthSnapshot]]]
    last_snapshot: DepthSnapshot | None


class FeatureStreamHub:
    """In-memory fanout hub for feature payload streaming.

    Goals:
    - One upstream stream per symbol (exchange_token string).
    - Many downstream WebSocket clients can subscribe without duplicating work.
    - Cache last payload per symbol for late joiners.

    Notes:
    - This is process-local cache (cleared on restart) by design.
    - Downstream logic is reused via `stream_feature_payloads`.
    """

    def __init__(
        self,
        *,
        cfg: FeatureEnv,
        exec_cfg: ExecutionEnv,
        trade_cfg: TradeabilityEnv,
        kill_cfg: KillSwitchEnv,
        stream_cfg: StreamingEnv,
        rolling_cfg: RollingEnv | None = None,
        ds: Any,
        instruments: InstrumentManager,
    ) -> None:
        self._cfg = cfg
        self._exec_cfg = exec_cfg
        self._trade_cfg = trade_cfg
        self._kill_cfg = kill_cfg
        self._stream_cfg = stream_cfg
        self._rolling_cfg = rolling_cfg
        self._ds = ds
        self._instruments = instruments
        self._lock = asyncio.Lock()
        self._by_symbol: dict[str, _SymbolEntry] = {}
        self._last_emit_s: dict[str, float] = {}

    async def subscribe(
        self,
        *,
        symbol: str,
        queue: asyncio.Queue[tuple[str, SnapshotFeaturePayload]],
    ) -> SnapshotFeaturePayload | None:
        """Subscribe a client queue to a symbol.

        Returns the last cached payload (if any) for immediate rendering.
        """

        async with self._lock:
            entry = self._by_symbol.get(symbol)
            if entry is None:
                entry = _SymbolEntry(task=None, subscribers=set(), last_payload=None)
                self._by_symbol[symbol] = entry

            entry.subscribers.add(queue)

            if entry.task is None or entry.task.done():
                entry.task = asyncio.create_task(self._run_symbol(symbol))

            return entry.last_payload

    async def unsubscribe(
        self,
        *,
        symbol: str,
        queue: asyncio.Queue[tuple[str, SnapshotFeaturePayload]],
    ) -> None:
        async with self._lock:
            entry = self._by_symbol.get(symbol)
            if entry is None:
                return

            entry.subscribers.discard(queue)

            # Stop upstream stream when nobody is listening. Keep `last_payload` cached.
            if not entry.subscribers and entry.task is not None and not entry.task.done():
                entry.task.cancel()

    async def close(self) -> None:
        async with self._lock:
            entries = list(self._by_symbol.values())
        for e in entries:
            if e.task is not None and not e.task.done():
                e.task.cancel()
        await asyncio.gather(
            *[e.task for e in entries if e.task is not None],
            return_exceptions=True,
        )

    async def _run_symbol(self, symbol: str) -> None:
        try:
            async for payload in stream_feature_payloads(
                cfg=self._cfg,
                exec_cfg=self._exec_cfg,
                risk_cfg=self._trade_cfg,
                kill_cfg=self._kill_cfg,
                stream_cfg=self._stream_cfg,
                rolling_cfg=self._rolling_cfg,
                ds=self._ds,
                symbol=symbol,
                instruments=self._instruments,
            ):
                await self._publish(symbol, payload)
        except asyncio.CancelledError:
            return
        except Exception:
            logger.exception("FeatureStreamHub symbol task failed", extra={"symbol": symbol})

    async def _publish(self, symbol: str, payload: SnapshotFeaturePayload) -> None:
        async with self._lock:
            entry = self._by_symbol.get(symbol)
            if entry is None:
                # No subscribers left; cache is not required in this case.
                return

            entry.last_payload = payload
            subscribers = list(entry.subscribers)

            now_s = asyncio.get_running_loop().time()
            interval_ms = int(self._stream_cfg.ui_emit_interval_ms)
            if interval_ms > 0:
                last_s = self._last_emit_s.get(symbol)
                if last_s is not None and (now_s - last_s) * 1000.0 < float(interval_ms):
                    return
                self._last_emit_s[symbol] = now_s

        # Publish outside the lock.
        for q in subscribers:
            try:
                q.put_nowait((symbol, payload))
            except asyncio.QueueFull:
                logger.warning("Subscriber queue full; dropping", extra={"symbol": symbol})


class SnapshotStreamHub:
    """In-memory fanout hub for raw snapshot streaming.

    Goals:
    - One upstream `ds.stream_snapshots(symbol=...)` per symbol.
    - Many downstream WebSocket clients can subscribe without duplicating upstream work.
    - Cache last snapshot per symbol for late joiners.

    Notes:
    - Process-local cache (cleared on restart).
    - The upstream datasource enforces canonical identity: `symbol` is the canonical exchange_token string.
    """

    def __init__(self, *, ds: Any, stream_cfg: StreamingEnv) -> None:
        self._ds = ds
        self._stream_cfg = stream_cfg
        self._lock = asyncio.Lock()
        self._by_symbol: dict[str, _SnapshotEntry] = {}
        self._last_emit_s: dict[str, float] = {}

    async def subscribe(
        self,
        *,
        symbol: str,
        queue: asyncio.Queue[tuple[str, DepthSnapshot]],
    ) -> DepthSnapshot | None:
        async with self._lock:
            entry = self._by_symbol.get(symbol)
            if entry is None:
                entry = _SnapshotEntry(task=None, subscribers=set(), last_snapshot=None)
                self._by_symbol[symbol] = entry

            entry.subscribers.add(queue)

            if entry.task is None or entry.task.done():
                entry.task = asyncio.create_task(self._run_symbol(symbol))

            return entry.last_snapshot

    async def unsubscribe(
        self,
        *,
        symbol: str,
        queue: asyncio.Queue[tuple[str, DepthSnapshot]],
    ) -> None:
        async with self._lock:
            entry = self._by_symbol.get(symbol)
            if entry is None:
                return

            entry.subscribers.discard(queue)

            if not entry.subscribers and entry.task is not None and not entry.task.done():
                entry.task.cancel()

    async def close(self) -> None:
        async with self._lock:
            entries = list(self._by_symbol.values())
        for e in entries:
            if e.task is not None and not e.task.done():
                e.task.cancel()
        await asyncio.gather(
            *[e.task for e in entries if e.task is not None],
            return_exceptions=True,
        )

    async def _run_symbol(self, symbol: str) -> None:
        try:
            async for snap in stream_processed_snapshots(ds=self._ds, symbol=symbol, cfg=self._stream_cfg):
                await self._publish(symbol, snap)
        except asyncio.CancelledError:
            return
        except Exception:
            logger.exception("SnapshotStreamHub symbol task failed", extra={"symbol": symbol})

    async def _publish(self, symbol: str, snap: DepthSnapshot) -> None:
        async with self._lock:
            entry = self._by_symbol.get(symbol)
            if entry is None:
                return

            entry.last_snapshot = snap
            subscribers = list(entry.subscribers)

            now_s = asyncio.get_running_loop().time()
            interval_ms = int(self._stream_cfg.ui_emit_interval_ms)
            if interval_ms > 0:
                last_s = self._last_emit_s.get(symbol)
                if last_s is not None and (now_s - last_s) * 1000.0 < float(interval_ms):
                    return
                self._last_emit_s[symbol] = now_s

        for q in subscribers:
            try:
                q.put_nowait((symbol, snap))
            except asyncio.QueueFull:
                logger.warning("Subscriber queue full; dropping", extra={"symbol": symbol})
