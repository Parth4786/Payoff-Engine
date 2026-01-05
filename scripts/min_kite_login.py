from kiteconnect import KiteConnect, KiteTicker
import requests
import json
import pandas as pd
import numpy as np
import logging
import time
import re
from typing import List, Dict, Union, Optional, Literal, Final, TypeVar, Protocol, Set, Tuple
from functools import lru_cache
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
import os
from dataclasses import dataclass
import pathlib # Import pathlib

# Configure structured logging with correlation IDs
logger = logging.getLogger(__name__)

# Constants for better maintainability
API_KEY: Final[str] = "s4pnzfflytntrgmf"
CREDENTIALS_API_URL: Final[str] = "http://110.172.21.62:5005/token/zerodha"
MONTHS: Final[List[str]] = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]
REQUEST_TIMEOUT: Final[int] = 10  # seconds
CACHE_DIR_NAME: Final[str] = "instrument_cache"
CACHE_EXPIRY_SECONDS: Final[int] = 24 * 60 * 60 # Cache valid for 24 hours

# Custom exception hierarchy for domain-specific errors
class KiteDataEngineError(Exception):
    """Base exception for all KiteDataEngine errors."""
    pass

class CredentialError(KiteDataEngineError):
    """Raised when there's an issue with authentication or credentials."""
    pass

class InstrumentDataError(KiteDataEngineError):
    """Raised when there's an issue with instrument data."""
    pass

class KiteDataEngine:
    def __init__(self, api_key: str = API_KEY, cache_dir: Optional[str] = None):
        """
        Initialize the KiteDataEngine with API credentials and cache settings.

        Parameters
        ----------
        api_key : str, optional
            Zerodha API key, defaults to the constant API_KEY.
        cache_dir : Optional[str], optional
            Directory to store instrument cache files. Defaults to a subdirectory
            named 'instrument_cache' in the script's directory.

        Raises
        ------
        CredentialError
            If unable to retrieve or set access token.
        IOError
            If unable to create the cache directory.

        Notes
        -----
        - Thread safety: Initialization is not thread-safe due to potential directory creation.
        - Memory usage: Minimal during initialization. Caching uses disk space.
        """
        self.correlation_id = f"kite-{int(time.time())}-{os.getpid()}"
        self.api_key = api_key

        # Setup cache directory
        if cache_dir is None:
            self.cache_dir = pathlib.Path(__file__).parent / CACHE_DIR_NAME
        else:
            self.cache_dir = pathlib.Path(cache_dir)

        try:
            self.cache_dir.mkdir(parents=True, exist_ok=True)
            logger.info(f"Using cache directory: {self.cache_dir}")
        except OSError as e:
            logger.error(f"Failed to create cache directory {self.cache_dir}: {e}", exc_info=True)
            # Decide if this is critical. For now, we'll raise but could potentially continue without caching.
            raise IOError(f"Failed to create cache directory {self.cache_dir}: {e}") from e

        logger.info(f"Initializing KiteDataEngine with correlation_id={self.correlation_id}")

        try:
            self.access_token = self._get_access_token()
            self.kite = KiteConnect(api_key=self.api_key)
            self.kite.set_access_token(self.access_token)
            logger.info("KiteConnect session established successfully")
        except Exception as e:
            logger.error(f"Failed to initialize KiteDataEngine: {str(e)}", exc_info=True)
            raise CredentialError(f"Failed to initialize KiteDataEngine: {str(e)}") from e

    def _get_access_token(self) -> str:
        """
        Retrieve Zerodha access token from credentials API.

        Returns
        -------
        str
            The access token for Zerodha API

        Raises
        ------
        CredentialError
            If unable to retrieve valid access token

        Notes
        -----
        - Network operation: Makes HTTP request to credential service
        - Performance: O(1) - single API call
        """
        start_time = time.time()
        logger.debug(f"Requesting access token from {CREDENTIALS_API_URL}")

        try:
            response = requests.get(
                CREDENTIALS_API_URL, 
                timeout=REQUEST_TIMEOUT,
                headers={"X-Correlation-ID": self.correlation_id}
            )

            if response.status_code != 200:
                raise CredentialError(
                    f"Failed to get access token: HTTP {response.status_code}, {response.text}"
                )

            execution_time = time.time() - start_time
            logger.info(f"Retrieved access token in {execution_time:.3f}s")
            return response.json()

        except requests.RequestException as e:
            logger.error(f"Network error retrieving access token: {str(e)}", exc_info=True)
            raise CredentialError(f"Network error retrieving access token: {str(e)}") from e
        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON in access token response: {str(e)}", exc_info=True)
            raise CredentialError(f"Invalid JSON in access token response: {str(e)}") from e

    def _instrument_df(self, exchange: str) -> pd.DataFrame:
        """
        Fetch instrument data directly from Zerodha API.

        Parameters
        ----------
        exchange : str
            The exchange for which to fetch instruments (e.g., 'NFO', 'NSE').

        Returns
        -------
        pd.DataFrame
            DataFrame containing instrument data.

        Raises
        ------
        InstrumentDataError
            If unable to fetch or process instrument data from the API.

        Notes
        -----
        - Performance: O(n) where n is number of instruments. Involves network call.
        - Memory usage: Temporarily holds all instruments for the exchange in memory.
        - This method does NOT use caching; it always fetches fresh data.
        """
        start_time = time.time()
        logger.debug(f"Fetching fresh instrument data for {exchange} from Zerodha API")

        try:
            instruments_list = self.kite.instruments(exchange)
            if not instruments_list:
                 logger.warning(f"Received empty instrument list for exchange: {exchange}")
                 return pd.DataFrame() # Return empty DataFrame if API gives empty list

            instruments = pd.DataFrame(instruments_list)

            # Add optimization fields for faster lookups
            if not instruments.empty and 'tradingsymbol' in instruments.columns:
                # Extract month information from tradingsymbol for faster filtering (if applicable)
                month_pattern = '|'.join(MONTHS)
                # Use regex=True explicitly for future pandas versions - REMOVED regex=True
                instruments['month'] = instruments['tradingsymbol'].str.extract(f'({month_pattern})', expand=False)
                # Handle cases where 'month' might not be extracted (NaN)
                instruments['month'] = instruments['month'].fillna('')

            execution_time = time.time() - start_time
            logger.info(f"Retrieved {len(instruments)} instruments for {exchange} from API in {execution_time:.3f}s")

            return instruments

        except Exception as e:
            logger.error(f"Failed to fetch instrument data for {exchange} from API: {str(e)}", exc_info=True)
            raise InstrumentDataError(f"Failed to fetch instrument data for {exchange} from API: {str(e)}") from e

    def _get_cache_filepath(self, exchange: str) -> pathlib.Path:
        """Constructs the filepath for the instrument cache file."""
        return self.cache_dir / f"{exchange.lower()}_instruments.csv"

    def get_instruments(self, exchange: str, force_refresh: bool = False) -> pd.DataFrame:
        """
        Get instrument data for a given exchange, using file-based caching.

        Parameters
        ----------
        exchange : str
            The exchange identifier (e.g., 'NFO', 'NSE', 'BSE', 'BFO').
        force_refresh : bool, optional
            If True, bypass the cache and fetch fresh data from the API. Defaults to False.

        Returns
        -------
        pd.DataFrame
            DataFrame containing instrument data. Returns an empty DataFrame if fetching fails
            and no valid cache exists.

        Raises
        ------
        InstrumentDataError
            If `force_refresh` is True and fetching from the API fails.
        IOError
            If reading from or writing to the cache file fails.

        Notes
        -----
        - Caching: Uses CSV files in the configured `cache_dir`. Cache is considered valid
          for `CACHE_EXPIRY_SECONDS`.
        - Thread-safety: File I/O operations might require external locking if multiple
          processes/threads access the same cache file concurrently, especially during writes.
          Reads are generally safe if writes are atomic (which pandas `to_csv` aims for, but
          isn't guaranteed across all filesystems/OS).
        - Performance: O(1) file read on cache hit. O(n) + file write on cache miss/expiry.
        """
        cache_file = self._get_cache_filepath(exchange)
        current_time = time.time()

        # Check cache validity
        use_cache = False
        if not force_refresh and cache_file.exists():
            try:
                cache_age = current_time - cache_file.stat().st_mtime
                if cache_age < CACHE_EXPIRY_SECONDS:
                    use_cache = True
                    logger.debug(f"Cache hit for {exchange}. File: {cache_file}, Age: {cache_age:.1f}s")
                else:
                    logger.info(f"Cache expired for {exchange}. File: {cache_file}, Age: {cache_age:.1f}s > {CACHE_EXPIRY_SECONDS}s")
            except OSError as e:
                logger.warning(f"Could not stat cache file {cache_file}: {e}. Will attempt refresh.", exc_info=True)
                use_cache = False # Force refresh if we can't read metadata
        elif force_refresh:
             logger.info(f"Forcing refresh for {exchange}, ignoring cache.")
        else:
             logger.info(f"Cache miss for {exchange}. File {cache_file} does not exist.")


        if use_cache:
            try:
                start_time = time.time()
                instruments_df = pd.read_csv(cache_file)
                load_time = time.time() - start_time
                logger.info(f"Loaded {len(instruments_df)} instruments for {exchange} from cache in {load_time:.3f}s")
                return instruments_df
            except (pd.errors.EmptyDataError, pd.errors.ParserError, FileNotFoundError, OSError) as e:
                logger.warning(f"Failed to load instruments from cache file {cache_file}: {e}. Attempting refresh.", exc_info=True)
                # Proceed to fetch fresh data if cache read fails
            except Exception as e: # Catch unexpected errors during cache read
                 logger.error(f"Unexpected error loading cache file {cache_file}: {e}. Attempting refresh.", exc_info=True)


        # Fetch fresh data if cache is invalid, missing, forced refresh, or read failed
        logger.info(f"Fetching fresh instruments for {exchange} and updating cache.")
        try:
            instruments_df = self._instrument_df(exchange)

            # Save the fresh data to cache
            try:
                # Ensure directory exists just in case
                self.cache_dir.mkdir(parents=True, exist_ok=True)
                instruments_df.to_csv(cache_file, index=False)
                logger.info(f"Successfully saved {len(instruments_df)} instruments for {exchange} to cache: {cache_file}")
            except IOError as e:
                logger.error(f"Failed to write instruments cache to {cache_file}: {e}", exc_info=True)
                # Decide how critical cache writing is. We can still return the fetched data.
                # raise IOError(f"Failed to write instruments cache to {cache_file}: {e}") from e # Optional: re-raise if writing is critical
            except Exception as e: # Catch unexpected errors during cache write
                 logger.error(f"Unexpected error writing cache file {cache_file}: {e}", exc_info=True)


            return instruments_df

        except InstrumentDataError as e:
            # If fetching fresh data fails, log the error but don't crash the caller
            # unless force_refresh was True. If not force_refresh, it means cache was
            # invalid/missing, and we couldn't refresh. Return empty df.
            logger.error(f"Failed to fetch fresh instrument data for {exchange}: {e}", exc_info=True)
            if force_refresh:
                raise # Re-raise the original error if refresh was explicitly forced
            else:
                logger.warning(f"Returning empty DataFrame for {exchange} as refresh failed and no valid cache exists.")
                return pd.DataFrame() # Return empty DataFrame as a fallback

    def print_instruments(self, exchange: str) -> None:
        """Print all available instruments for an exchange (uses caching)."""
        try:
            instruments = self.get_instruments(exchange)
            if not instruments.empty:
                # Use pandas option for better display if many columns/rows
                with pd.option_context('display.max_rows', None, 'display.max_columns', None):
                    print(f"\n--- Instruments for {exchange} ---")
                    print(instruments)
                    print(f"--- End of Instruments for {exchange} ({len(instruments)} rows) ---")
            else:
                print(f"No instruments found or loaded for exchange: {exchange}")
        except (InstrumentDataError, IOError) as e:
            logger.error(f"Could not get or print instruments for {exchange}: {e}")
            print(f"Error retrieving instruments for {exchange}: {e}")


if __name__ == "__main__":
    # --- Logging Setup ---
    # 1. Define a default correlation ID (can be updated later)
    current_correlation_id = f"main-init-{int(time.time())}"

    # 2. Create a custom LogRecord factory
    old_factory = logging.getLogRecordFactory()
    def record_factory(*args, **kwargs):
        record = old_factory(*args, **kwargs)
        record.correlation_id = current_correlation_id # Add correlation_id to all records
        return record
    logging.setLogRecordFactory(record_factory)

    # 3. Configure basicConfig with the format string
    logging.basicConfig(
        format='%(asctime)s - %(name)s - %(levelname)s - [%(correlation_id)s] - %(message)s',
        level=logging.INFO
    )
    # --- End Logging Setup ---


    # Example usage with profiling
    try:
        start_time_main = time.time()
        kite_data_engine = KiteDataEngine()

        # Update the global correlation ID after engine initializes
        # This is a simple approach for __main__; contextvars are better for complex apps
        current_correlation_id = kite_data_engine.correlation_id
        # Log that the ID is updated (optional)
        logger.info(f"Correlation ID updated to: {current_correlation_id}")


        print(f"Access token retrieved: ...{kite_data_engine.access_token[-6:]}") # Avoid printing full token

        # Fetch and print instruments for multiple exchanges (will use/create cache)
        exchanges_to_test = ["NFO", "NSE", "BSE", "BFO"] # Added CDS for variety

        for exch in exchanges_to_test:
            print(f"\nGetting instruments for {exch}...")
            exch_start_time = time.time()
            # Use force_refresh=True on first run if you want to ensure cache is created/updated
            # instruments_df = kite_data_engine.get_instruments(exch, force_refresh=True)
            instruments_df = kite_data_engine.get_instruments(exch)
            exch_end_time = time.time()
            if not instruments_df.empty:
                print(f"Successfully got {len(instruments_df)} instruments for {exch} in {exch_end_time - exch_start_time:.3f}s.")
                # Optionally print head/tail instead of all
                # print(instruments_df.head())
            else:
                 print(f"Got empty DataFrame for {exch} in {exch_end_time - exch_start_time:.3f}s.")


        # Example: Force refresh for one exchange
        print("\nForcing refresh for NFO...")
        exch_start_time = time.time()
        instruments_nfo_fresh = kite_data_engine.get_instruments("NFO", force_refresh=True)
        exch_end_time = time.time()
        print(f"Successfully got {len(instruments_nfo_fresh)} fresh instruments for NFO in {exch_end_time - exch_start_time:.3f}s.")


        # Example: Print instruments (will use cache if valid)
        # kite_data_engine.print_instruments("NFO")
        print(kite_data_engine.kite.historical_data(3861249,'2025-12-15','2025-12-30',interval='15minute'))

        end_time_main = time.time()
        print(f"\nTotal execution time: {end_time_main - start_time_main:.3f}s")

    except (CredentialError, InstrumentDataError, IOError) as e:
        logger.critical(f"Critical error during execution: {e}", exc_info=True)
        print(f"An error occurred: {e}")
    except Exception as e:
        logger.exception(f"An unexpected error occurred in main execution block: {e}")
        print(f"An unexpected error occurred: {e}")

