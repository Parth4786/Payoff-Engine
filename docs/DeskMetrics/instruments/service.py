"""Instruments service - cached instrument data with search/resolution."""
from __future__ import annotations

import logging
import re
import time
from dataclasses import dataclass, field
from datetime import datetime, date
from functools import lru_cache
from pathlib import Path
from typing import Any

import pandas as pd

from backend.app.infra.kite_client import get_kite_client, KiteClientError
from backend.app.settings import get_settings

logger = logging.getLogger(__name__)

# Supported exchanges
EXCHANGES = ["NSE", "NFO", "BSE", "BFO", "CDS", "MCX"]

# Month abbreviations for expiry parsing
MONTHS = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]

# Indices with weekly expiry (use YYMDD format)
WEEKLY_EXPIRY_SYMBOLS = {"NIFTY", "BANKNIFTY", "FINNIFTY", "MIDCPNIFTY", "SENSEX", "BANKEX"}

# Cache settings
CACHE_EXPIRY_SECONDS = 24 * 60 * 60  # 24 hours


@dataclass
class Instrument:
    """Instrument data structure."""
    instrument_token: int
    exchange_token: int
    tradingsymbol: str
    name: str
    exchange: str
    segment: str
    instrument_type: str
    lot_size: int
    tick_size: float
    expiry: str | None = None
    strike: float | None = None
    
    def to_dict(self) -> dict[str, Any]:
        return {
            "instrument_token": self.instrument_token,
            "exchange_token": self.exchange_token,
            "tradingsymbol": self.tradingsymbol,
            "name": self.name,
            "exchange": self.exchange,
            "segment": self.segment,
            "instrument_type": self.instrument_type,
            "lot_size": self.lot_size,
            "tick_size": self.tick_size,
            "expiry": self.expiry,
            "strike": self.strike,
        }


@dataclass
class InstrumentCache:
    """In-memory cache for instruments DataFrame."""
    df: pd.DataFrame = field(default_factory=pd.DataFrame)
    last_refresh: float = 0
    
    @property
    def is_valid(self) -> bool:
        if self.df.empty:
            return False
        return (time.time() - self.last_refresh) < CACHE_EXPIRY_SECONDS
    
    def refresh(self, df: pd.DataFrame) -> None:
        self.df = df
        self.last_refresh = time.time()


# Global cache instance
_instrument_cache = InstrumentCache()


class InstrumentService:
    """Service for instrument data management."""
    
    def __init__(self):
        self.cache_dir = Path(get_settings().file_store_path) / "instruments"
        self.cache_dir.mkdir(parents=True, exist_ok=True)
    
    def _get_cache_path(self, exchange: str) -> Path:
        return self.cache_dir / f"{exchange.lower()}_instruments.csv"
    
    def _load_from_disk_cache(self, exchange: str) -> pd.DataFrame | None:
        """Load instruments from disk cache if valid."""
        cache_path = self._get_cache_path(exchange)
        if not cache_path.exists():
            return None
        
        try:
            cache_age = time.time() - cache_path.stat().st_mtime
            if cache_age > CACHE_EXPIRY_SECONDS:
                logger.info(f"Disk cache expired for {exchange}")
                return None
            
            df = pd.read_csv(cache_path)
            logger.info(f"Loaded {len(df)} instruments for {exchange} from disk cache")
            return df
        except Exception as e:
            logger.warning(f"Failed to load disk cache for {exchange}: {e}")
            return None
    
    def _save_to_disk_cache(self, exchange: str, df: pd.DataFrame) -> None:
        """Save instruments to disk cache."""
        try:
            cache_path = self._get_cache_path(exchange)
            df.to_csv(cache_path, index=False)
            logger.info(f"Saved {len(df)} instruments for {exchange} to disk cache")
        except Exception as e:
            logger.warning(f"Failed to save disk cache for {exchange}: {e}")
    
    def _fetch_from_kite(self, exchange: str) -> pd.DataFrame:
        """Fetch instruments from Kite API."""
        kite = get_kite_client()
        if kite is None:
            raise KiteClientError("Kite client not available")
        
        instruments_list = kite.instruments(exchange)
        if not instruments_list:
            return pd.DataFrame()
        
        df = pd.DataFrame(instruments_list)
        
        # Add computed fields for search
        if not df.empty and "tradingsymbol" in df.columns:
            # Extract month for filtering
            month_pattern = "|".join(MONTHS)
            df["month"] = df["tradingsymbol"].str.extract(f"({month_pattern})", expand=False)
            df["month"] = df["month"].fillna("")
            
            # Normalize search text
            df["search_text"] = (
                df["tradingsymbol"].str.lower() + " " +
                df["name"].fillna("").str.lower() + " " +
                df["exchange"].str.lower()
            )
        
        return df
    
    def get_instruments(self, exchange: str, force_refresh: bool = False) -> pd.DataFrame:
        """Get instruments for an exchange with caching."""
        exchange = exchange.upper()
        
        if not force_refresh:
            # Try disk cache first
            cached_df = self._load_from_disk_cache(exchange)
            if cached_df is not None:
                return cached_df
        
        # Fetch from Kite
        logger.info(f"Fetching instruments for {exchange} from Kite API")
        try:
            df = self._fetch_from_kite(exchange)
            if not df.empty:
                self._save_to_disk_cache(exchange, df)
            return df
        except KiteClientError as e:
            logger.error(f"Failed to fetch instruments: {e}")
            # Fall back to potentially stale cache
            cache_path = self._get_cache_path(exchange)
            if cache_path.exists():
                logger.info(f"Using stale cache for {exchange}")
                return pd.read_csv(cache_path)
            return pd.DataFrame()
    
    def get_all_instruments(self, force_refresh: bool = False) -> pd.DataFrame:
        """Get instruments from all exchanges."""
        global _instrument_cache
        
        if not force_refresh and _instrument_cache.is_valid:
            return _instrument_cache.df
        
        all_dfs = []
        for exchange in EXCHANGES:
            try:
                df = self.get_instruments(exchange, force_refresh)
                if not df.empty:
                    all_dfs.append(df)
            except Exception as e:
                logger.warning(f"Failed to get instruments for {exchange}: {e}")
        
        if not all_dfs:
            return pd.DataFrame()
        
        combined = pd.concat(all_dfs, ignore_index=True)
        _instrument_cache.refresh(combined)
        
        logger.info(f"Combined {len(combined)} instruments from {len(all_dfs)} exchanges")
        return combined
    
    def search(
        self,
        query: str,
        exchange: str | None = None,
        segment: str | None = None,
        instrument_type: str | None = None,
        limit: int = 50,
    ) -> list[dict[str, Any]]:
        """Fuzzy search for instruments.
        
        Supports queries like:
        - "nifty dec fut"
        - "banknifty 45000 ce"
        - "reliance"
        
        Prioritizes F&O instruments for margin calculations.
        """
        df = self.get_all_instruments()
        if df.empty:
            return []
        
        # Normalize query
        query_parts = query.lower().split()
        
        # Build filter mask
        mask = pd.Series([True] * len(df))
        
        # Filter by exchange if specified
        if exchange:
            mask &= df["exchange"].str.upper() == exchange.upper()
        
        # Filter by segment if specified
        if segment:
            mask &= df["segment"].str.upper() == segment.upper()
        
        # Filter by instrument type if specified
        if instrument_type:
            mask &= df["instrument_type"].str.upper() == instrument_type.upper()
        
        # Fuzzy match on query parts
        has_fno_hint = any(p in ["fut", "future", "futures", "ce", "pe", "opt", "option", "options"] for p in query_parts)
        has_month_hint = any(p in [m.lower() for m in MONTHS] for p in query_parts)
        has_strike_hint = any(p.isdigit() and len(p) >= 3 for p in query_parts)  # Strike prices are 3+ digits
        
        if "search_text" in df.columns:
            for part in query_parts:
                # Handle special patterns
                if part in [m.lower() for m in MONTHS]:
                    mask &= df["month"].str.lower() == part
                elif part in ["ce", "pe"]:
                    mask &= df["instrument_type"].str.lower() == part
                elif part in ["fut", "future", "futures"]:
                    mask &= df["instrument_type"].str.lower() == "fut"
                elif part.isdigit() and len(part) >= 3:
                    # Likely a strike price
                    mask &= df["strike"].fillna(0).astype(str).str.contains(part)
                else:
                    # General fuzzy match
                    mask &= df["search_text"].str.contains(part, na=False)
        
        # Apply mask
        results = df[mask].copy()
        
        if results.empty:
            return []
        
        # Add scoring for sorting - prioritize F&O for margin calculator
        # Score: Higher = better match
        results["_score"] = 0
        
        # Prioritize NFO/BFO (tradeable F&O) over INDICES
        results.loc[results["exchange"].isin(["NFO", "BFO"]), "_score"] += 100
        results.loc[results["exchange"].isin(["NSE", "BSE"]), "_score"] += 50
        results.loc[results["segment"] == "INDICES", "_score"] -= 50
        
        # If user typed FUT/CE/PE hints, strongly prefer those
        if has_fno_hint or has_month_hint or has_strike_hint:
            # Prefer F&O exchanges
            results.loc[results["exchange"].isin(["NFO", "BFO", "CDS"]), "_score"] += 50
            # Deprioritize indices even more
            results.loc[results["segment"] == "INDICES", "_score"] -= 100
        
        # Prefer exact tradingsymbol starts
        first_part = query_parts[0] if query_parts else ""
        if first_part:
            results.loc[results["tradingsymbol"].str.lower().str.startswith(first_part), "_score"] += 30
        
        # Prefer shorter symbols (more likely to be the base instrument)
        results["_score"] -= results["tradingsymbol"].str.len() / 10
        
        # Sort by score descending
        results = results.sort_values("_score", ascending=False)
        
        # Remove score column and limit
        results = results.drop(columns=["_score"]).head(limit)
        
        # Convert to list of dicts
        return results.to_dict(orient="records")
    
    def build_tradingsymbol(
        self,
        underlying: str,
        expiry: str | date,
        instrument_type: str,
        strike: float | int | None = None,
        exchange: str = "NFO",
        is_weekly: bool | None = None,
    ) -> str | None:
        """Build a Kite-format tradingsymbol from components.
        
        Args:
            underlying: Base symbol (e.g., "NIFTY", "BANKNIFTY", "RELIANCE")
            expiry: Expiry date as YYYY-MM-DD string or date object
            instrument_type: "FUT", "CE", or "PE"
            strike: Strike price (required for CE/PE, ignored for FUT)
            exchange: Exchange (NFO, BFO, etc.)
            is_weekly: Force weekly (True) or monthly (False) format, or auto-detect (None)
        
        Returns:
            Kite tradingsymbol string or None if unable to build
            
        Kite format patterns:
            Weekly options (NIFTY/BANKNIFTY/FINNIFTY etc): SYMBOL + YY + M + DD + STRIKE + CE/PE
                - Month: 1-9 for Jan-Sep, O/N/D for Oct/Nov/Dec
                - Example: NIFTY2510224000CE (Jan 02, 2025 24000 CE)
            
            Monthly options: SYMBOL + YY + MMM + STRIKE + CE/PE
                - Example: NIFTY26JAN26000CE (Jan 2026 monthly, 26000 CE)
                - Example: RELIANCE25MAY1410CE (May 2025 monthly, 1410 CE)
            
            Futures: SYMBOL + YY + MMM + FUT
                - Example: NIFTY25MAYFUT
        """
        underlying = underlying.upper().strip()
        instrument_type = instrument_type.upper().strip()
        
        # Parse expiry date
        if isinstance(expiry, str):
            try:
                # Try multiple date formats
                for fmt in ["%Y-%m-%d", "%d-%m-%Y", "%d/%m/%Y", "%Y/%m/%d", "%d-%b-%Y", "%d %b %Y"]:
                    try:
                        expiry_date = datetime.strptime(expiry, fmt).date()
                        break
                    except ValueError:
                        continue
                else:
                    logger.warning(f"Could not parse expiry date: {expiry}")
                    return None
            except Exception as e:
                logger.warning(f"Error parsing expiry: {e}")
                return None
        else:
            expiry_date = expiry
        
        year_2digit = str(expiry_date.year)[-2:]  # "25" for 2025
        month_3letter = MONTHS[expiry_date.month - 1]  # "MAY" for month 5
        
        # Determine if this is a weekly or monthly expiry
        is_weekly_eligible = underlying in WEEKLY_EXPIRY_SYMBOLS
        
        if instrument_type == "FUT":
            # Futures always use: SYMBOL + YY + MMM + FUT
            # e.g., NIFTY25MAYFUT, RELIANCE25JUNFUT
            return f"{underlying}{year_2digit}{month_3letter}FUT"
        
        elif instrument_type in ("CE", "PE"):
            if strike is None:
                logger.warning(f"Strike required for {instrument_type}")
                return None
            
            # Format strike - remove decimal if whole number
            strike_val = int(strike) if float(strike) == int(strike) else strike
            strike_str = str(strike_val)
            
            # Decide between weekly (YYMDD) and monthly (YYMMM) format
            # If is_weekly is not specified, we'll generate BOTH formats and try to match
            if is_weekly is None:
                # For weekly-eligible symbols, return weekly format
                # (most common case for active trading)
                if is_weekly_eligible:
                    month_code = self._get_month_code(expiry_date.month)
                    day_2digit = f"{expiry_date.day:02d}"
                    return f"{underlying}{year_2digit}{month_code}{day_2digit}{strike_str}{instrument_type}"
                else:
                    # Stocks only have monthly options
                    return f"{underlying}{year_2digit}{month_3letter}{strike_str}{instrument_type}"
            elif is_weekly:
                # Forced weekly format
                month_code = self._get_month_code(expiry_date.month)
                day_2digit = f"{expiry_date.day:02d}"
                return f"{underlying}{year_2digit}{month_code}{day_2digit}{strike_str}{instrument_type}"
            else:
                # Forced monthly format
                return f"{underlying}{year_2digit}{month_3letter}{strike_str}{instrument_type}"
        
        else:
            logger.warning(f"Unknown instrument type: {instrument_type}")
            return None
    
    def _get_month_code(self, month: int) -> str:
        """Get single-character month code for weekly expiry.
        
        1-9 for Jan-Sep, O/N/D for Oct/Nov/Dec
        """
        if 1 <= month <= 9:
            return str(month)
        elif month == 10:
            return "O"
        elif month == 11:
            return "N"
        elif month == 12:
            return "D"
        return str(month)
    
    def build_and_validate_symbol(
        self,
        underlying: str,
        expiry: str | date,
        instrument_type: str,
        strike: float | int | None = None,
        exchange: str = "NFO",
    ) -> dict[str, Any]:
        """Build tradingsymbol and validate it exists in instruments.
        
        For indices with both weekly and monthly options, tries both formats
        to find a match.
        
        Returns instrument data if found, or suggestions if not.
        """
        underlying_upper = underlying.upper().strip()
        is_weekly_eligible = underlying_upper in WEEKLY_EXPIRY_SYMBOLS
        instrument_type_upper = instrument_type.upper().strip()
        
        # Get instruments DataFrame for lookup
        df = self.get_all_instruments()
        
        # For weekly-eligible symbols with options, try both weekly and monthly formats
        symbols_to_try = []
        
        if instrument_type_upper in ("CE", "PE") and is_weekly_eligible:
            # Try weekly format first (more common for active trading)
            weekly_symbol = self.build_tradingsymbol(
                underlying=underlying,
                expiry=expiry,
                instrument_type=instrument_type,
                strike=strike,
                exchange=exchange,
                is_weekly=True,
            )
            if weekly_symbol:
                symbols_to_try.append(("weekly", weekly_symbol))
            
            # Also try monthly format
            monthly_symbol = self.build_tradingsymbol(
                underlying=underlying,
                expiry=expiry,
                instrument_type=instrument_type,
                strike=strike,
                exchange=exchange,
                is_weekly=False,
            )
            if monthly_symbol:
                symbols_to_try.append(("monthly", monthly_symbol))
        else:
            # For futures or stock options, just one format
            built_symbol = self.build_tradingsymbol(
                underlying=underlying,
                expiry=expiry,
                instrument_type=instrument_type,
                strike=strike,
                exchange=exchange,
            )
            if built_symbol:
                symbols_to_try.append(("default", built_symbol))
        
        if not symbols_to_try:
            return {
                "success": False,
                "built_symbol": None,
                "instrument": None,
                "message": "Could not construct tradingsymbol from provided components",
                "suggestions": [],
            }
        
        if df.empty:
            return {
                "success": False,
                "built_symbol": symbols_to_try[0][1],
                "instrument": None,
                "message": "No instrument data available",
                "suggestions": [],
            }
        
        # Try each built symbol format
        for format_type, built_symbol in symbols_to_try:
            # Exact match
            mask = (df["tradingsymbol"].str.upper() == built_symbol.upper())
            if exchange:
                mask &= (df["exchange"].str.upper() == exchange.upper())
            
            exact_matches = df[mask]
            if not exact_matches.empty:
                instrument = exact_matches.iloc[0].to_dict()
                # Clean NaN values
                for k, v in instrument.items():
                    if pd.isna(v):
                        instrument[k] = None
                return {
                    "success": True,
                    "built_symbol": built_symbol,
                    "instrument": instrument,
                    "message": f"Symbol found ({format_type} format)",
                    "suggestions": [],
                }
        
        # No exact match found - return first tried symbol with suggestions
        first_symbol = symbols_to_try[0][1]
        
        # Search for same underlying + instrument type near same expiry
        mask = df["name"].str.upper() == underlying_upper
        if instrument_type_upper:
            mask &= df["instrument_type"].str.upper() == instrument_type_upper
        if exchange:
            mask &= df["exchange"].str.upper() == exchange.upper()
        
        similar = df[mask].head(10)
        suggestions = []
        for _, row in similar.iterrows():
            sugg = row.to_dict()
            for k, v in sugg.items():
                if pd.isna(v):
                    sugg[k] = None
            suggestions.append(sugg)
        
        tried_symbols = [s[1] for s in symbols_to_try]
        return {
            "success": False,
            "built_symbol": first_symbol,
            "instrument": None,
            "message": f"Tried symbols {tried_symbols} - none found in instruments",
            "suggestions": suggestions,
        }

    def resolve_symbol(
        self,
        symbol: str,
        exchange: str | None = None,
    ) -> dict[str, Any] | None:
        """Resolve a trading symbol to instrument data.
        
        Tries exact match first, then fuzzy match.
        """
        df = self.get_all_instruments()
        if df.empty:
            return None
        
        symbol_upper = symbol.upper().strip()
        
        # Try exact match first
        mask = df["tradingsymbol"].str.upper() == symbol_upper
        if exchange:
            mask &= df["exchange"].str.upper() == exchange.upper()
        
        exact_matches = df[mask]
        if not exact_matches.empty:
            return exact_matches.iloc[0].to_dict()
        
        # Try partial match
        mask = df["tradingsymbol"].str.upper().str.contains(symbol_upper, na=False)
        if exchange:
            mask &= df["exchange"].str.upper() == exchange.upper()
        
        partial_matches = df[mask]
        if not partial_matches.empty:
            # Return best match (shortest symbol that contains query)
            sorted_matches = partial_matches.sort_values(by="tradingsymbol", key=lambda x: x.str.len())
            return sorted_matches.iloc[0].to_dict()
        
        return None
    
    async def resolve_symbols_bulk(
        self,
        symbols: list[str],
    ) -> list[dict[str, Any]]:
        """Resolve multiple symbols at once.
        
        Args:
            symbols: List of symbol strings
        
        Returns:
            List of resolution results with status, instrument, confidence
        """
        df = self.get_all_instruments()
        results = []
        
        for symbol in symbols:
            symbol_clean = symbol.upper().strip()
            
            if df.empty:
                results.append({
                    "input_symbol": symbol,
                    "status": "unmatched",
                    "instrument": None,
                    "confidence": 0,
                    "suggestions": [],
                })
                continue
            
            # Try exact match first
            mask = df["tradingsymbol"].str.upper() == symbol_clean
            exact_matches = df[mask]
            
            if not exact_matches.empty:
                instrument = exact_matches.iloc[0].to_dict()
                # Clean up NaN values
                for k, v in instrument.items():
                    if pd.isna(v):
                        instrument[k] = None
                results.append({
                    "input_symbol": symbol,
                    "status": "matched",
                    "instrument": instrument,
                    "confidence": 1.0,
                    "suggestions": [],
                })
                continue
            
            # Try partial match (symbol contains query)
            mask = df["tradingsymbol"].str.upper().str.contains(symbol_clean, na=False)
            partial_matches = df[mask].head(5)
            
            if not partial_matches.empty:
                # Best match is shortest symbol containing query
                sorted_matches = partial_matches.sort_values(
                    by="tradingsymbol", 
                    key=lambda x: x.str.len()
                )
                best_match = sorted_matches.iloc[0].to_dict()
                
                # Clean up NaN values
                for k, v in best_match.items():
                    if pd.isna(v):
                        best_match[k] = None
                
                # Calculate confidence based on similarity
                confidence = len(symbol_clean) / len(best_match.get("tradingsymbol", symbol_clean))
                confidence = min(confidence, 0.95)  # Cap at 0.95 for partial matches
                
                # Build suggestions from other matches
                suggestions = []
                for _, row in sorted_matches.iloc[1:4].iterrows():
                    sugg = row.to_dict()
                    for k, v in sugg.items():
                        if pd.isna(v):
                            sugg[k] = None
                    suggestions.append(sugg)
                
                results.append({
                    "input_symbol": symbol,
                    "status": "partial",
                    "instrument": best_match,
                    "confidence": confidence,
                    "suggestions": suggestions,
                })
                continue
            
            # Try fuzzy search on name/search_text
            if "search_text" in df.columns:
                query_parts = symbol_clean.lower().split()
                mask = pd.Series([True] * len(df))
                for part in query_parts:
                    mask &= df["search_text"].str.contains(part.lower(), na=False)
                
                fuzzy_matches = df[mask].head(5)
                
                if not fuzzy_matches.empty:
                    suggestions = []
                    for _, row in fuzzy_matches.iterrows():
                        sugg = row.to_dict()
                        for k, v in sugg.items():
                            if pd.isna(v):
                                sugg[k] = None
                        suggestions.append(sugg)
                    
                    results.append({
                        "input_symbol": symbol,
                        "status": "unmatched",
                        "instrument": None,
                        "confidence": 0,
                        "suggestions": suggestions,
                    })
                    continue
            
            # No match found
            results.append({
                "input_symbol": symbol,
                "status": "unmatched",
                "instrument": None,
                "confidence": 0,
                "suggestions": [],
            })
        
        return results


# Singleton instance
_instrument_service: InstrumentService | None = None


def get_instrument_service() -> InstrumentService:
    """Get the instrument service singleton."""
    global _instrument_service
    if _instrument_service is None:
        _instrument_service = InstrumentService()
    return _instrument_service
