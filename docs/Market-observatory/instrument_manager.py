from __future__ import annotations

import csv
import bisect
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from typing import Iterable, Literal

from core.config import load_instrument_master_dir


class InstrumentMasterError(ValueError):
    """Raised when the instrument master is missing or inconsistent."""


ConflictPolicy = Literal["raise", "keep_first", "keep_last"]


@dataclass(frozen=True, slots=True)
class InstrumentInfo:
    """One row from Kite instrument master.

    Key identity:
    - `canonical_symbol` is the unique internal identifier: `{exchange}:{exchange_token}`.
    - This avoids conflicts where same exchange_token appears in different exchanges.

    Note:
    - `instrument_token` is used for Kite WS subscription (not used in ClickHouse).
    - ClickHouse uses `exchange` column (1=NSE, 2=NFO) + `instrument_id` (== exchange_token).
    """

    instrument_token: int
    exchange_token: int
    tradingsymbol: str
    name: str

    expiry: date | None
    strike: float | None

    tick_size: float | None
    lot_size: int | None

    instrument_type: str
    segment: str
    exchange: str

    @property
    def canonical_symbol(self) -> str:
        """Unique identifier: {exchange}:{exchange_token}."""
        return f"{self.exchange}:{self.exchange_token}"


@dataclass(slots=True)
class InstrumentManager:
    """Loads and serves instrument master metadata.

    This is intentionally deterministic and strict.
    Keying is by canonical_symbol = `{exchange}:{exchange_token}` to avoid conflicts
    where same exchange_token appears across different exchanges.
    """

    by_canonical_symbol: dict[str, InstrumentInfo]
    by_instrument_token: dict[int, InstrumentInfo]
    by_tradingsymbol: dict[str, tuple[InstrumentInfo, ...]]
    tradingsymbol_keys_sorted: tuple[str, ...]

    # Legacy compatibility - use with care (may have conflicts across exchanges)
    by_exchange_token: dict[int, InstrumentInfo]

    @classmethod
    def from_directory(
        cls,
        *,
        instrument_master_dir: str | Path,
        exchanges: Iterable[str] | None = None,
        on_conflict: ConflictPolicy = "raise",
        require_any: bool = True,
    ) -> "InstrumentManager":
        base = Path(instrument_master_dir)
        if not base.exists() or not base.is_dir():
            raise InstrumentMasterError(f"Instrument master dir not found: {base}")

        files: list[Path] = []

        if exchanges is None:
            files = sorted(base.glob("instrument_list_*.csv"))
            # Back-compat with the repo's inspiration CSV naming.
            if not files:
                files = sorted(base.glob("*_instruments.csv"))
        else:
            for ex in exchanges:
                ex_norm = ex.strip().lower()
                if not ex_norm:
                    continue
                # Prefer our canonical naming.
                f = base / f"instrument_list_{ex_norm}.csv"
                if f.exists():
                    files.append(f)
                    continue
                # Fall back to inspiration naming.
                f2 = base / f"{ex_norm}_instruments.csv"
                if f2.exists():
                    files.append(f2)

        if require_any and not files:
            raise InstrumentMasterError(
                f"No instrument master CSVs found in: {base}. Expected instrument_list_*.csv or *_instruments.csv"
            )

        return cls.from_csv_files(csv_files=files, on_conflict=on_conflict)

    @classmethod
    def from_env(
        cls,
        *,
        env_file: str | None = None,
        exchanges: Iterable[str] | None = None,
        on_conflict: ConflictPolicy = "raise",
    ) -> "InstrumentManager":
        """Create an InstrumentManager using `KITE_INSTRUMENT_MASTER_DIR` from env."""

        instrument_master_dir = load_instrument_master_dir(env_file=env_file)
        return cls.from_directory(
            instrument_master_dir=instrument_master_dir,
            exchanges=exchanges,
            on_conflict=on_conflict,
        )

    @classmethod
    def from_csv_files(
        cls,
        *,
        csv_files: Iterable[str | Path],
        on_conflict: ConflictPolicy = "raise",
    ) -> "InstrumentManager":
        by_canonical_symbol: dict[str, InstrumentInfo] = {}
        by_exchange_token: dict[int, InstrumentInfo] = {}
        by_instrument_token: dict[int, InstrumentInfo] = {}

        for file_path in csv_files:
            path = Path(file_path)
            if not path.exists():
                continue

            _load_csv_into(
                by_canonical_symbol,
                by_exchange_token,
                by_instrument_token,
                path=path,
                on_conflict=on_conflict,
            )

        by_tradingsymbol_list: dict[str, list[InstrumentInfo]] = {}
        for info in by_canonical_symbol.values():
            ts = (info.tradingsymbol or "").strip()
            if not ts:
                continue
            key = ts.lower()
            by_tradingsymbol_list.setdefault(key, []).append(info)

        by_tradingsymbol: dict[str, tuple[InstrumentInfo, ...]] = {
            k: tuple(v) for k, v in by_tradingsymbol_list.items()
        }

        tradingsymbol_keys_sorted: tuple[str, ...] = tuple(sorted(by_tradingsymbol.keys()))

        return cls(
            by_canonical_symbol=by_canonical_symbol,
            by_exchange_token=by_exchange_token,
            by_instrument_token=by_instrument_token,
            by_tradingsymbol=by_tradingsymbol,
            tradingsymbol_keys_sorted=tradingsymbol_keys_sorted,
        )

    def get(self, exchange_token: int) -> InstrumentInfo | None:
        """Legacy get by exchange_token. May return wrong result if same exchange_token exists in multiple exchanges."""
        return self.by_exchange_token.get(int(exchange_token))

    def get_by_canonical_symbol(self, canonical_symbol: str) -> InstrumentInfo | None:
        """Get by canonical symbol (exchange:exchange_token)."""
        return self.by_canonical_symbol.get(canonical_symbol)

    def get_by_instrument_token(self, instrument_token: int) -> InstrumentInfo | None:
        return self.by_instrument_token.get(int(instrument_token))

    def find_all_by_tradingsymbol(self, tradingsymbol: str) -> tuple[InstrumentInfo, ...]:
        key = (tradingsymbol or "").strip().lower()
        if not key:
            return ()
        return self.by_tradingsymbol.get(key, ())

    def search_tradingsymbols(self, query: str, *, limit: int = 50) -> list[InstrumentInfo]:
        """Search instruments by tradingsymbol prefix (case-insensitive).
        
        This is for frontend autocomplete. Returns up to `limit` results sorted by tradingsymbol.
        """
        query = (query or "").strip().lower()
        if not query:
            return []

        keys = self.tradingsymbol_keys_sorted
        if not keys:
            return []

        # Prefix range via binary search.
        start = bisect.bisect_left(keys, query)
        end = bisect.bisect_right(keys, query + "\uffff")

        # Candidate gathering can explode for option chains (tens of thousands of strikes).
        # We bound the number of raw candidates we gather, then rank + truncate.
        max_candidates = max(1000, min(50_000, limit * 800))
        candidates: list[InstrumentInfo] = []

        # Always prioritize the exact tradingsymbol bucket first (if present).
        exact_infos = self.by_tradingsymbol.get(query)
        if exact_infos:
            candidates.extend(exact_infos)

        # Then expand to other prefixed tradingsymbol buckets.
        for k in keys[start:end]:
            if k == query:
                continue
            infos = self.by_tradingsymbol.get(k)
            if infos:
                candidates.extend(infos)
                if len(candidates) >= max_candidates:
                    break

        if not candidates:
            return []

        def _instrument_type_rank(instrument_type: str) -> int:
            it = (instrument_type or "").strip().upper()
            if it == "EQ":
                return 0
            # Kite uses FUTSTK/FUTIDX (and sometimes FUT) for futures.
            if it == "FUT" or it.startswith("FUT") or "FUT" in it:
                return 1
            # Options tend to dominate in volume; rank them lower.
            if it in {"CE", "PE"} or it.endswith("CE") or it.endswith("PE") or "OPT" in it:
                return 3
            return 2

        exchange_rank = {
            "NSE": 0,
            "BSE": 1,
            "NFO": 2,
            "BFO": 3,
            "MCX": 4,
        }

        def _rank(info: InstrumentInfo) -> tuple[int, int, int, int, str]:
            ts = (info.tradingsymbol or "").strip().lower()
            exact = 0 if ts == query else 1
            it_rank = _instrument_type_rank(info.instrument_type)
            ex_rank = exchange_rank.get((info.exchange or "").strip().upper(), 9)
            return (exact, it_rank, ex_rank, len(info.tradingsymbol or ""), ts)

        # Deduplicate (by canonical_symbol) after ranking.
        seen: set[str] = set()
        out: list[InstrumentInfo] = []
        for info in sorted(candidates, key=_rank):
            if info.canonical_symbol in seen:
                continue
            seen.add(info.canonical_symbol)
            out.append(info)
            if len(out) >= limit:
                break
        return out

    def resolve_tradingsymbol(
        self,
        tradingsymbol: str,
        *,
        exchange: str | None = None,
        segment: str | None = None,
    ) -> InstrumentInfo:
        """Resolve a UI-facing tradingsymbol to a single instrument.

        This is intentionally strict and deterministic:
        - If no match exists, raise.
        - If multiple matches exist, require disambiguation (exchange/segment) or raise.
        """

        candidates = list(self.find_all_by_tradingsymbol(tradingsymbol))

        if exchange is not None:
            ex = exchange.strip().upper()
            if ex:
                candidates = [c for c in candidates if (c.exchange or "").strip().upper() == ex]

        if segment is not None:
            seg = segment.strip().upper()
            if seg:
                candidates = [c for c in candidates if (c.segment or "").strip().upper() == seg]

        if not candidates:
            raise InstrumentMasterError(f"Unknown tradingsymbol: {tradingsymbol}")

        if len(candidates) > 1:
            details = ", ".join(
                sorted({f"{c.exchange}:{c.segment}:{c.exchange_token}" for c in candidates})
            )
            raise InstrumentMasterError(
                f"Ambiguous tradingsymbol: {tradingsymbol}. Candidates: {details}"
            )

        return candidates[0]

    def exchange_token_for_instrument_token(self, instrument_token: int) -> int | None:
        info = self.get_by_instrument_token(instrument_token)
        if info is None:
            return None
        return info.exchange_token

    def canonical_symbol_for_instrument_token(self, instrument_token: int) -> str | None:
        """Get canonical symbol (exchange:exchange_token) for an instrument_token."""
        info = self.get_by_instrument_token(instrument_token)
        if info is None:
            return None
        return info.canonical_symbol

    def require_exchange_token_for_instrument_token(self, instrument_token: int) -> int:
        info = self.get_by_instrument_token(instrument_token)
        if info is None:
            raise InstrumentMasterError(
                f"Missing instrument for instrument_token={instrument_token}"
            )
        return info.exchange_token

    def require_canonical_symbol_for_instrument_token(self, instrument_token: int) -> str:
        """Get canonical symbol or raise."""
        info = self.get_by_instrument_token(instrument_token)
        if info is None:
            raise InstrumentMasterError(
                f"Missing instrument for instrument_token={instrument_token}"
            )
        return info.canonical_symbol

    def require(self, exchange_token: int) -> InstrumentInfo:
        """Legacy require by exchange_token."""
        info = self.get(exchange_token)
        if info is None:
            raise InstrumentMasterError(f"Missing instrument for exchange_token={exchange_token}")
        return info

    def require_by_canonical_symbol(self, canonical_symbol: str) -> InstrumentInfo:
        """Require by canonical symbol (exchange:exchange_token)."""
        info = self.get_by_canonical_symbol(canonical_symbol)
        if info is None:
            raise InstrumentMasterError(f"Missing instrument for canonical_symbol={canonical_symbol}")
        return info

    def subset(self, exchange_tokens: Iterable[int]) -> dict[int, InstrumentInfo]:
        out: dict[int, InstrumentInfo] = {}
        for tok in exchange_tokens:
            tok_i = int(tok)
            info = self.by_exchange_token.get(tok_i)
            if info is not None:
                out[tok_i] = info
        return out

    def subset_by_canonical_symbols(self, symbols: Iterable[str]) -> dict[str, InstrumentInfo]:
        """Get subset by canonical symbols."""
        out: dict[str, InstrumentInfo] = {}
        for sym in symbols:
            info = self.by_canonical_symbol.get(sym)
            if info is not None:
                out[sym] = info
        return out

    def as_json_map(self, exchange_tokens: Iterable[int] | None = None) -> dict[str, dict[str, object]]:
        items = self.by_exchange_token if exchange_tokens is None else self.subset(exchange_tokens)
        return {str(k): _instrument_to_jsonable(v) for k, v in items.items()}

    def as_canonical_json_map(self, symbols: Iterable[str] | None = None) -> dict[str, dict[str, object]]:
        """Get JSON map keyed by canonical symbol."""
        items = self.by_canonical_symbol if symbols is None else self.subset_by_canonical_symbols(symbols)
        return {k: _instrument_to_jsonable(v) for k, v in items.items()}


def _instrument_to_jsonable(info: InstrumentInfo) -> dict[str, object]:
    return {
        "canonical_symbol": info.canonical_symbol,
        "instrument_token": info.instrument_token,
        "exchange_token": info.exchange_token,
        "tradingsymbol": info.tradingsymbol,
        "name": info.name,
        "expiry": info.expiry.isoformat() if info.expiry else None,
        "strike": info.strike,
        "tick_size": info.tick_size,
        "lot_size": info.lot_size,
        "instrument_type": info.instrument_type,
        "segment": info.segment,
        "exchange": info.exchange,
    }


def _load_csv_into(
    by_canonical_symbol: dict[str, InstrumentInfo],
    by_exchange_token: dict[int, InstrumentInfo],
    by_instrument_token: dict[int, InstrumentInfo],
    *,
    path: Path,
    on_conflict: ConflictPolicy,
) -> None:
    with path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            info = _parse_instrument_row(row, source=str(path))
            if info is None:
                continue

            # Primary key is now canonical_symbol (exchange:exchange_token)
            canonical_sym = info.canonical_symbol

            existing_canonical = by_canonical_symbol.get(canonical_sym)
            if existing_canonical is None:
                by_canonical_symbol[canonical_sym] = info
            elif existing_canonical == info:
                pass
            else:
                if on_conflict == "keep_first":
                    pass  # Keep existing
                elif on_conflict == "keep_last":
                    by_canonical_symbol[canonical_sym] = info
                else:
                    raise InstrumentMasterError(
                        "Conflicting instrument master entries for canonical_symbol="
                        f"{canonical_sym}:\nexisting={existing_canonical}\nnew={info}"
                    )

            # instrument_token lookup (used for Kite WS → canonical_symbol mapping)
            existing_it = by_instrument_token.get(info.instrument_token)
            if existing_it is None:
                by_instrument_token[info.instrument_token] = info
            elif existing_it == info:
                pass
            else:
                # instrument_token collisions are not expected; don't guess.
                raise InstrumentMasterError(
                    "Conflicting instrument master entries for instrument_token="
                    f"{info.instrument_token}:\nexisting={existing_it}\nnew={info}"
                )

            # Legacy exchange_token lookup (may have conflicts across exchanges)
            # Use keep_first policy to avoid raising on expected conflicts
            existing = by_exchange_token.get(info.exchange_token)
            if existing is None:
                by_exchange_token[info.exchange_token] = info
            # For legacy lookup, just keep_first silently (conflicts expected)


def _parse_instrument_row(row: dict[str, str], *, source: str) -> InstrumentInfo | None:
    # Some CSVs have a leading index column with an empty header; ignore it.
    row = {k: v for k, v in row.items() if k and k.strip()}

    try:
        instrument_token = int(float(row["instrument_token"]))
        exchange_token = int(float(row["exchange_token"]))
    except Exception as e:
        raise InstrumentMasterError(f"Invalid instrument_token/exchange_token in {source}: {row}") from e

    if exchange_token <= 0:
        return None

    tradingsymbol = (row.get("tradingsymbol") or "").strip()
    name = (row.get("name") or "").strip()

    expiry = _parse_date(row.get("expiry"))
    strike = _parse_float(row.get("strike"))

    tick_size = _parse_float(row.get("tick_size"))
    if tick_size is not None and tick_size <= 0:
        tick_size = None

    lot_size = _parse_int(row.get("lot_size"))
    if lot_size is not None and lot_size <= 0:
        lot_size = None

    instrument_type = (row.get("instrument_type") or "").strip()
    segment = (row.get("segment") or "").strip()
    exchange = (row.get("exchange") or "").strip()

    return InstrumentInfo(
        instrument_token=instrument_token,
        exchange_token=exchange_token,
        tradingsymbol=tradingsymbol,
        name=name,
        expiry=expiry,
        strike=strike,
        tick_size=tick_size,
        lot_size=lot_size,
        instrument_type=instrument_type,
        segment=segment,
        exchange=exchange,
    )


def _parse_int(v: str | None) -> int | None:
    if v is None:
        return None
    v = v.strip()
    if not v:
        return None
    try:
        return int(float(v))
    except ValueError:
        return None


def _parse_float(v: str | None) -> float | None:
    if v is None:
        return None
    v = v.strip()
    if not v:
        return None
    try:
        return float(v)
    except ValueError:
        return None


def _parse_date(v: str | None) -> date | None:
    if v is None:
        return None
    v = v.strip()
    if not v:
        return None
    try:
        return date.fromisoformat(v)
    except ValueError:
        return None
