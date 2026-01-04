from __future__ import annotations

import asyncio
import heapq
import logging
import time
from collections.abc import AsyncIterator, Iterable
from dataclasses import asdict, dataclass
from datetime import datetime, timedelta
from typing import Any

from core.config import ExecutionEnv, FeatureEnv, KillSwitchEnv, RollingEnv, StreamingEnv, TradeabilityEnv
from core.instrument_manager import InstrumentManager
from core.models import DepthSnapshot
from core.payloads import SnapshotFeaturePayload, build_payload
from features.engine import FeatureSnapshot, compute_features_with_instruments

from execution.hint_engine import ExecutionHintEngine
from execution.models import ExecutionDecision, ExecutionHint
from risk.kill_switches import KillSwitchEngine
from risk.models import KillSwitchReport, RiskReport, TradeabilityReport
from risk.tradeability import TradeabilityEngine

from streaming.assembler import assemble_snapshot
from streaming.dedup import Deduplicator
from streaming.gap_detector import GapDetector
from streaming.rolling import RollingConfig, RollingFeatures, RollingWindowTracker


logger = logging.getLogger(__name__)


def _rolling_config_from_env(cfg: RollingEnv | None) -> RollingConfig:
    """Convert RollingEnv to RollingConfig, with sensible defaults."""
    if cfg is None:
        return RollingConfig()
    return RollingConfig(
        window_ticks=cfg.window_ticks,
        min_ticks=cfg.min_ticks,
        absorption_volume_percentile=cfg.absorption_volume_percentile,
        absorption_price_move_max=cfg.absorption_price_move_max,
        pressure_windows_sec=cfg.pressure_windows_sec,
        pressure_dt_floor_ms=cfg.pressure_dt_floor_ms,
        pressure_min_fill_ratio=cfg.pressure_min_fill_ratio,
    )


@dataclass(frozen=True, slots=True)
class StreamDiagnostics:
    dedup_reorder_drops_total: int
    dedup_duplicate_drops_total: int
    is_gap: bool


def _is_newer(prev: DepthSnapshot | None, cur: DepthSnapshot) -> bool:
    if prev is None:
        return True
    # Discard out-of-order snapshots to keep the downstream deterministic.
    return cur.capture_time >= prev.capture_time


def compute_feature_payloads_from_snapshots(
    *,
    cfg: FeatureEnv,
    snapshots: Iterable[DepthSnapshot],
    instruments: InstrumentManager,
) -> list[SnapshotFeaturePayload]:
    prev: DepthSnapshot | None = None
    out: list[SnapshotFeaturePayload] = []

    for snap in sorted(list(snapshots), key=lambda s: s.capture_time):
        if not _is_newer(prev, snap):
            continue

        feats = compute_features_with_instruments(cfg=cfg, snapshot=snap, prev_snapshot=prev, instruments=instruments)
        # v1 helper retained for older callsites: use safe defaults.
        tradeability = TradeabilityReport(score=0.0, reasons=("v1_compat",))
        kill_switch = KillSwitchReport(active=False, reasons=("v1_compat",))
        risk = RiskReport(tradeability=tradeability, kill_switch=kill_switch)
        execution = ExecutionDecision(hint=ExecutionHint.WAIT, reasons=("v1_compat",))
        out.append(
            build_payload(
                snapshot=snap,
                features=feats,
                execution=execution,
                risk=risk,
                instruments=instruments,
            )
        )
        prev = snap

    return out


def process_snapshots_offline(*, snapshots: Iterable[DepthSnapshot], cfg: StreamingEnv) -> list[DepthSnapshot]:
    """Apply the same defensive streaming stages to a finite snapshot sequence.

    This exists to enforce the non-negotiable invariant:
    replay and streaming share identical downstream logic.
    """

    return [s for s, _ in process_snapshots_offline_with_diagnostics(snapshots=snapshots, cfg=cfg)]


def process_snapshots_offline_with_diagnostics(
    *,
    snapshots: Iterable[DepthSnapshot],
    cfg: StreamingEnv,
) -> list[tuple[DepthSnapshot, StreamDiagnostics]]:
    prev: DepthSnapshot | None = None
    dedup = Deduplicator()
    gap = GapDetector(gap_threshold_ms=cfg.gap_threshold_ms)

    out: list[tuple[DepthSnapshot, StreamDiagnostics]] = []
    for update in sorted(list(snapshots), key=lambda s: s.capture_time):
        assembled = assemble_snapshot(prev=prev, update=update)

        if not dedup.accept(assembled.capture_time):
            continue

        is_gap = gap.is_gap(assembled.capture_time)
        if is_gap and not assembled.is_stale:
            from dataclasses import replace

            assembled = replace(assembled, is_stale=True)

        diag = StreamDiagnostics(
            dedup_reorder_drops_total=dedup.reorder_drops_total,
            dedup_duplicate_drops_total=dedup.duplicate_drops_total,
            is_gap=is_gap,
        )
        out.append((assembled, diag))
        prev = assembled

    return out


def compute_feature_payloads_from_snapshots_hardened(
    *,
    cfg: FeatureEnv,
    stream_cfg: StreamingEnv,
    snapshots: Iterable[DepthSnapshot],
    instruments: InstrumentManager,
) -> list[SnapshotFeaturePayload]:
    """Replay-safe feature payloads.

    Applies the same hardening stages as live streaming before feature computation.
    """

    processed = process_snapshots_offline(snapshots=snapshots, cfg=stream_cfg)
    prev: DepthSnapshot | None = None
    out: list[SnapshotFeaturePayload] = []

    for snap in processed:
        if not _is_newer(prev, snap):
            continue

        feats = compute_features_with_instruments(cfg=cfg, snapshot=snap, prev_snapshot=prev, instruments=instruments)
        # v1 helper retained for older callsites: use safe defaults.
        tradeability = TradeabilityReport(score=0.0, reasons=("v1_compat",))
        kill_switch = KillSwitchReport(active=False, reasons=("v1_compat",))
        risk = RiskReport(tradeability=tradeability, kill_switch=kill_switch)
        execution = ExecutionDecision(hint=ExecutionHint.WAIT, reasons=("v1_compat",))
        out.append(
            build_payload(
                snapshot=snap,
                features=feats,
                execution=execution,
                risk=risk,
                instruments=instruments,
            )
        )
        prev = snap
def compute_desk_payloads_from_snapshots_hardened(
    *,
    cfg: FeatureEnv,
    exec_cfg: ExecutionEnv,
    risk_cfg: TradeabilityEnv,
    kill_cfg: KillSwitchEnv,
    stream_cfg: StreamingEnv,
    rolling_cfg: RollingEnv | None = None,
    snapshots: Iterable[DepthSnapshot],
    instruments: InstrumentManager,
) -> list[SnapshotFeaturePayload]:
    """Desk-grade v2 payloads for replay-safe finite snapshot sequences.

    This is the reference path to enforce replay/stream parity.
    """

    processed = process_snapshots_offline_with_diagnostics(snapshots=snapshots, cfg=stream_cfg)

    prev: DepthSnapshot | None = None
    trade_engine = TradeabilityEngine(cfg=risk_cfg)
    kill_engine = KillSwitchEngine(cfg=kill_cfg)
    exec_engine = ExecutionHintEngine(cfg=exec_cfg)
    rolling_tracker = RollingWindowTracker(cfg=_rolling_config_from_env(rolling_cfg))

    out: list[SnapshotFeaturePayload] = []
    for snap, diag in processed:
        if not _is_newer(prev, snap):
            continue

        feats = compute_features_with_instruments(cfg=cfg, snapshot=snap, prev_snapshot=prev, instruments=instruments)

        # IMPORTANT: prefer canonical symbol lookups to avoid cross-exchange exchange_token collisions.
        info = instruments.get_by_canonical_symbol(snap.symbol) or instruments.get(snap.instrument_id)
        tick_size = info.tick_size if info is not None else None

        tradeability = trade_engine.update(feature=feats, tick_size=tick_size)
        kill_switch = kill_engine.evaluate(
            snapshot=snap,
            features=feats,
            tradeability=tradeability,
            dedup_regressions_total=diag.dedup_reorder_drops_total,
        )

        execution = exec_engine.decide(
            feature=feats,
            tick_size=tick_size,
            kill_switch_active=kill_switch.active,
        )

        # Rolling window features
        rolling = rolling_tracker.update(
            capture_time=snap.capture_time,
            spread=feats.spread,
            midprice=feats.midprice,
            traded_qty_delta=feats.traded_qty_delta,
            mid_change=feats.mid_change,
            ofi=feats.ofi,
        )

        risk = RiskReport(tradeability=tradeability, kill_switch=kill_switch)
        out.append(
            build_payload(
                snapshot=snap,
                features=feats,
                execution=execution,
                risk=risk,
                rolling=rolling,
                instruments=instruments,
            )
        )
        prev = snap

    return out


async def stream_feature_payloads(
    *,
    cfg: FeatureEnv,
    exec_cfg: ExecutionEnv,
    risk_cfg: TradeabilityEnv,
    kill_cfg: KillSwitchEnv,
    stream_cfg: StreamingEnv,
    rolling_cfg: RollingEnv | None = None,
    ds,
    symbol: str,
    instruments: InstrumentManager,
) -> AsyncIterator[SnapshotFeaturePayload]:
    prev: DepthSnapshot | None = None

    trade_engine = TradeabilityEngine(cfg=risk_cfg)
    kill_engine = KillSwitchEngine(cfg=kill_cfg)
    exec_engine = ExecutionHintEngine(cfg=exec_cfg)
    rolling_tracker = RollingWindowTracker(cfg=_rolling_config_from_env(rolling_cfg))

    async for snap, diag in stream_processed_snapshots_with_diagnostics(ds=ds, symbol=symbol, cfg=stream_cfg):
        if not _is_newer(prev, snap):
            continue

        feats = compute_features_with_instruments(cfg=cfg, snapshot=snap, prev_snapshot=prev, instruments=instruments)

        # IMPORTANT: prefer canonical symbol lookups to avoid cross-exchange exchange_token collisions.
        info = instruments.get_by_canonical_symbol(snap.symbol) or instruments.get(snap.instrument_id)
        tick_size = info.tick_size if info is not None else None

        tradeability = trade_engine.update(feature=feats, tick_size=tick_size)
        kill_switch = kill_engine.evaluate(
            snapshot=snap,
            features=feats,
            tradeability=tradeability,
            dedup_regressions_total=diag.dedup_reorder_drops_total,
        )

        execution = exec_engine.decide(
            feature=feats,
            tick_size=tick_size,
            kill_switch_active=kill_switch.active,
        )

        # Rolling window features
        rolling = rolling_tracker.update(
            capture_time=snap.capture_time,
            spread=feats.spread,
            midprice=feats.midprice,
            traded_qty_delta=feats.traded_qty_delta,
            mid_change=feats.mid_change,
            ofi=feats.ofi,
        )

        risk = RiskReport(tradeability=tradeability, kill_switch=kill_switch)
        yield build_payload(
            snapshot=snap,
            features=feats,
            execution=execution,
            risk=risk,
            rolling=rolling,
            instruments=instruments,
        )
        prev = snap


async def stream_processed_snapshots(*, ds, symbol: str, cfg: StreamingEnv) -> AsyncIterator[DepthSnapshot]:
    """Stream snapshots with defensive hardening stages.

    Stages (per docs/STREAMING_ENGINE.md):
    - assembler: marks partial snapshots (future: merge partial updates)
    - dedup/order: drops duplicates and regressions
    - gap detection: marks snapshots stale when gaps exceed threshold

    Note: this cannot mark staleness in the absence of new ticks (no heartbeat).
    """

    async for snap, _ in stream_processed_snapshots_with_diagnostics(ds=ds, symbol=symbol, cfg=cfg):
        yield snap


async def stream_processed_snapshots_with_diagnostics(
    *,
    ds,
    symbol: str,
    cfg: StreamingEnv,
) -> AsyncIterator[tuple[DepthSnapshot, StreamDiagnostics]]:
    """Streaming hardening with diagnostics for desk-grade v2 gating."""

    prev: DepthSnapshot | None = None
    dedup = Deduplicator()
    gap = GapDetector(gap_threshold_ms=cfg.gap_threshold_ms)

    async for update in ds.stream_snapshots(symbol=symbol):
        assembled = assemble_snapshot(prev=prev, update=update)

        if not dedup.accept(assembled.capture_time):
            continue

        is_gap = gap.is_gap(assembled.capture_time)
        if is_gap and not assembled.is_stale:
            from dataclasses import replace

            assembled = replace(assembled, is_stale=True)

        diag = StreamDiagnostics(
            dedup_reorder_drops_total=dedup.reorder_drops_total,
            dedup_duplicate_drops_total=dedup.duplicate_drops_total,
            is_gap=is_gap,
        )

        yield assembled, diag
        prev = assembled


async def merge_streams(
    streams: dict[str, AsyncIterator[Any]],
) -> AsyncIterator[tuple[str, Any]]:
    """Merge multiple async iterators into one stream of (symbol, item)."""

    q: asyncio.Queue[tuple[str, Any]] = asyncio.Queue(maxsize=2000)

    async def _pump(sym: str, it: AsyncIterator[Any]) -> None:
        async for item in it:
            await q.put((sym, item))

    tasks = [asyncio.create_task(_pump(sym, it)) for sym, it in streams.items()]

    try:
        while True:
            sym, item = await q.get()
            yield sym, item
    finally:
        for t in tasks:
            t.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)


async def merge_payload_streams_by_time(
    streams: dict[str, AsyncIterator[SnapshotFeaturePayload]],
) -> AsyncIterator[tuple[str, SnapshotFeaturePayload]]:
    """Merge multiple per-symbol payload streams into global time order.

    This is useful for replay mode where a shared time axis is desired.

    Ordering key: payload.snapshot.capture_time.
    """

    def _naive(dt: datetime) -> datetime:
        return dt.replace(tzinfo=None) if dt.tzinfo is not None else dt

    heap: list[tuple[datetime, int, str, SnapshotFeaturePayload]] = []
    counter = 0
    iters: dict[str, AsyncIterator[SnapshotFeaturePayload]] = dict(streams)

    async def _prime(sym: str, it: AsyncIterator[SnapshotFeaturePayload]) -> None:
        nonlocal counter
        try:
            first = await it.__anext__()
        except StopAsyncIteration:
            return
        heapq.heappush(heap, (_naive(first.snapshot.capture_time), counter, sym, first))
        counter += 1

    # Prime all streams.
    for sym, it in list(iters.items()):
        await _prime(sym, it)

    while heap:
        _, _, sym, payload = heapq.heappop(heap)
        yield sym, payload

        it = iters.get(sym)
        if it is None:
            continue
        try:
            nxt = await it.__anext__()
        except StopAsyncIteration:
            iters.pop(sym, None)
            continue
        heapq.heappush(heap, (_naive(nxt.snapshot.capture_time), counter, sym, nxt))
        counter += 1


async def stream_replay_feature_payloads(
    *,
    cfg: FeatureEnv,
    exec_cfg: ExecutionEnv,
    risk_cfg: TradeabilityEnv,
    kill_cfg: KillSwitchEnv,
    stream_cfg: StreamingEnv,
    rolling_cfg: RollingEnv | None = None,
    ds,
    symbol: str,
    instruments: InstrumentManager,
    start_time: datetime,
    end_time: datetime,
    batch_window_ms: int = 5_000,
    max_rows_per_batch: int = 10_000,
    speed: float = 1.0,
    speed_handle: Any | None = None,
    max_sleep_ms: int = 200,
    prefetch: bool = True,
) -> AsyncIterator[SnapshotFeaturePayload]:
    """Replay-mode feature stream using the same downstream logic as live.

    Key invariant: hardening + features + execution + risk logic matches the live stream.
    Only the upstream tick reader is different (ClickHouse time-bounded queries).

    Notes:
    - Reads ClickHouse (or any replay-capable datasource) in bounded batches.
    - Maintains per-symbol state across batches (prev snapshot, dedup, gap, engines).
    - Optional pacing: sleeps according to capture_time deltas / speed (capped).
    """

    if batch_window_ms <= 0:
        raise ValueError("batch_window_ms must be > 0")
    if max_rows_per_batch <= 0:
        raise ValueError("max_rows_per_batch must be > 0")
    if speed <= 0:
        raise ValueError("speed must be > 0")
    if max_sleep_ms < 0:
        raise ValueError("max_sleep_ms must be >= 0")

    def _naive(dt: datetime) -> datetime:
        return dt.replace(tzinfo=None) if dt.tzinfo is not None else dt

    prev: DepthSnapshot | None = None
    dedup = Deduplicator()
    gap = GapDetector(gap_threshold_ms=stream_cfg.gap_threshold_ms)

    trade_engine = TradeabilityEngine(cfg=risk_cfg)
    kill_engine = KillSwitchEngine(cfg=kill_cfg)
    exec_engine = ExecutionHintEngine(cfg=exec_cfg)
    rolling_tracker = RollingWindowTracker(cfg=_rolling_config_from_env(rolling_cfg))

    start_time = _naive(start_time)
    end_time = _naive(end_time)
    cursor = start_time
    prev_emit_time: datetime | None = None
    full_window = timedelta(milliseconds=int(batch_window_ms))
    # Bootstrap with a small initial window to reduce time-to-first-payload.
    # This avoids the UI feeling "stuck" while ClickHouse answers a large first query.
    initial_window = timedelta(milliseconds=min(int(batch_window_ms), 2_000))
    did_yield = False

    async def _fetch_batch(
        start_at: datetime,
    ) -> tuple[datetime, list[DepthSnapshot]]:
        window = initial_window if not did_yield else full_window
        t0 = time.perf_counter()
        batch_end = min(end_time, start_at + window)
        updates: list[DepthSnapshot] = await ds.replay_snapshots(
            symbol=symbol,
            start_time=start_at,
            end_time=batch_end,
            max_rows=max_rows_per_batch,
        )
        dt_ms = (time.perf_counter() - t0) * 1000.0
        if dt_ms >= 500.0:
            logger.info(
                "Replay batch fetch slow",
                extra={
                    "symbol": symbol,
                    "start_time": start_at.isoformat(),
                    "end_time": batch_end.isoformat(),
                    "rows": len(updates),
                    "max_rows": max_rows_per_batch,
                    "fetch_ms": int(dt_ms),
                },
            )
        if len(updates) >= max_rows_per_batch:
            logger.info(
                "Replay batch hit max_rows_per_batch",
                extra={
                    "symbol": symbol,
                    "start_time": start_at.isoformat(),
                    "end_time": batch_end.isoformat(),
                    "rows": len(updates),
                    "max_rows": max_rows_per_batch,
                },
            )
        if not updates:
            return batch_end, []
        # Ensure deterministic ordering.
        updates = sorted(updates, key=lambda s: _naive(s.capture_time))
        return batch_end, updates

    fetch_task: asyncio.Task[tuple[datetime, list[DepthSnapshot]]] | None = None
    if prefetch:
        fetch_task = asyncio.create_task(_fetch_batch(cursor))

    while cursor <= end_time:
        if fetch_task is None:
            batch_end, updates = await _fetch_batch(cursor)
        else:
            batch_end, updates = await fetch_task

        # Decide next cursor and schedule the next fetch before processing updates.
        if not updates:
            next_cursor = batch_end + timedelta(microseconds=1)
        else:
            last_time = _naive(updates[-1].capture_time)
            next_cursor = max(cursor, last_time + timedelta(microseconds=1))

        if prefetch and next_cursor <= end_time:
            fetch_task = asyncio.create_task(_fetch_batch(next_cursor))
        else:
            fetch_task = None

        if not updates:
            cursor = next_cursor
            continue

        for update in updates:
            # Normalize capture_time to avoid tz-aware/naive mixing downstream.
            ct = _naive(update.capture_time)
            if ct is not update.capture_time:
                from dataclasses import replace

                update = replace(update, capture_time=ct)

            assembled = assemble_snapshot(prev=prev, update=update)

            if not dedup.accept(assembled.capture_time):
                continue

            is_gap = gap.is_gap(assembled.capture_time)
            if is_gap and not assembled.is_stale:
                from dataclasses import replace

                assembled = replace(assembled, is_stale=True)

            diag = StreamDiagnostics(
                dedup_reorder_drops_total=dedup.reorder_drops_total,
                dedup_duplicate_drops_total=dedup.duplicate_drops_total,
                is_gap=is_gap,
            )

            if not _is_newer(prev, assembled):
                continue

            feats = compute_features_with_instruments(
                cfg=cfg,
                snapshot=assembled,
                prev_snapshot=prev,
                instruments=instruments,
            )

            # IMPORTANT: prefer canonical symbol lookups to avoid cross-exchange exchange_token collisions.
            info = instruments.get_by_canonical_symbol(assembled.symbol) or instruments.get(assembled.instrument_id)
            tick_size = info.tick_size if info is not None else None

            tradeability = trade_engine.update(feature=feats, tick_size=tick_size)
            kill_switch = kill_engine.evaluate(
                snapshot=assembled,
                features=feats,
                tradeability=tradeability,
                dedup_regressions_total=diag.dedup_reorder_drops_total,
            )
            execution = exec_engine.decide(
                feature=feats,
                tick_size=tick_size,
                kill_switch_active=kill_switch.active,
            )
            rolling = rolling_tracker.update(
                capture_time=assembled.capture_time,
                spread=feats.spread,
                midprice=feats.midprice,
                traded_qty_delta=feats.traded_qty_delta,
                mid_change=feats.mid_change,
                ofi=feats.ofi,
            )

            payload = build_payload(
                snapshot=assembled,
                features=feats,
                execution=execution,
                risk=RiskReport(tradeability=tradeability, kill_switch=kill_switch),
                rolling=rolling,
                instruments=instruments,
            )

            # Optional pacing to mimic live; cap sleeps to avoid stalling on long gaps.
            if prev_emit_time is not None and max_sleep_ms > 0:
                dt_s = (assembled.capture_time - prev_emit_time).total_seconds()
                if dt_s > 0:
                    current_speed = float(speed)
                    if speed_handle is not None:
                        try:
                            current_speed = float(getattr(speed_handle, "value"))
                        except Exception:
                            current_speed = float(speed)
                    if current_speed <= 0:
                        current_speed = float(speed)
                    sleep_s = min(float(max_sleep_ms) / 1000.0, dt_s / float(current_speed))
                    if sleep_s > 0:
                        await asyncio.sleep(sleep_s)

            yield payload
            did_yield = True
            prev = assembled
            prev_emit_time = assembled.capture_time

        cursor = next_cursor
