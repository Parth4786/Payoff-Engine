"""Kite client wrapper - singleton KiteConnect instance for the entire backend.

Based on min_kite_login.py pattern:
- API key from settings
- Access token fetched from URL OR from settings directly
- Single KiteConnect instance created at startup and reused everywhere
"""
from __future__ import annotations

import logging
import time
from typing import Any

import requests

from backend.app.settings import get_settings

logger = logging.getLogger(__name__)

# Check if kiteconnect is available
try:
    from kiteconnect import KiteConnect
    KITE_AVAILABLE = True
except ImportError:
    KITE_AVAILABLE = False
    KiteConnect = None  # type: ignore


class KiteClientError(Exception):
    """Error from Kite client operations."""
    pass


class CredentialError(KiteClientError):
    """Raised when there's an issue with authentication or credentials."""
    pass


# Request timeout for fetching token from URL
REQUEST_TIMEOUT = 10  # seconds


def _fetch_access_token_from_url(url: str) -> str:
    """Fetch access token from a token provider URL.
    
    Args:
        url: URL to fetch the access token from
        
    Returns:
        Access token string
        
    Raises:
        CredentialError: If unable to retrieve valid access token
    """
    start_time = time.time()
    logger.debug(f"Fetching access token from {url}")
    
    try:
        response = requests.get(url, timeout=REQUEST_TIMEOUT)
        
        if response.status_code != 200:
            raise CredentialError(
                f"Failed to get access token: HTTP {response.status_code}, {response.text}"
            )
        
        # The response could be plain text token or JSON
        content_type = response.headers.get("Content-Type", "")
        if "application/json" in content_type:
            token = response.json()
            # Handle if it's a dict with a token field
            if isinstance(token, dict):
                token = token.get("access_token") or token.get("token") or str(token)
        else:
            token = response.text.strip()
        
        execution_time = time.time() - start_time
        logger.info(f"Retrieved access token from URL in {execution_time:.3f}s")
        return str(token)
        
    except requests.RequestException as e:
        logger.error(f"Network error retrieving access token: {e}", exc_info=True)
        raise CredentialError(f"Network error retrieving access token: {e}") from e
    except Exception as e:
        logger.error(f"Error parsing access token response: {e}", exc_info=True)
        raise CredentialError(f"Error parsing access token response: {e}") from e


class KiteClient:
    """Wrapper around KiteConnect for margin and charges APIs.
    
    Credentials are read from settings:
    - KITE_API_KEY: Kite Connect API key (required)
    - KITE_ACCESS_TOKEN: Access token (optional if KITE_TOKEN_URL is set)
    - KITE_TOKEN_URL: URL to fetch access token (optional if KITE_ACCESS_TOKEN is set)
    
    The KiteConnect instance is created once and reused.
    """
    
    def __init__(
        self,
        api_key: str | None = None,
        access_token: str | None = None,
        token_url: str | None = None,
    ):
        """Initialize Kite client.
        
        Args:
            api_key: Kite API key (or from settings)
            access_token: Access token (or from settings, or fetched from token_url)
            token_url: URL to fetch access token (or from settings)
        """
        if not KITE_AVAILABLE:
            raise KiteClientError(
                "kiteconnect package not installed. Install with: pip install kiteconnect"
            )
        
        settings = get_settings()
        
        self.api_key = api_key or settings.kite_api_key or ""
        self.token_url = token_url or settings.kite_token_url
        
        # Get access token: either from param, settings, or fetch from URL
        if access_token:
            self.access_token = access_token
        elif settings.kite_access_token:
            self.access_token = settings.kite_access_token
        elif self.token_url:
            logger.info(f"Fetching access token from URL: {self.token_url}")
            self.access_token = _fetch_access_token_from_url(self.token_url)
        else:
            self.access_token = ""
        
        self._kite: "KiteConnect | None" = None
        
        # Initialize KiteConnect if we have credentials
        if self.is_configured:
            self._init_kite()
    
    def _init_kite(self) -> None:
        """Initialize the KiteConnect instance."""
        logger.info("Initializing KiteConnect session")
        self._kite = KiteConnect(api_key=self.api_key)
        self._kite.set_access_token(self.access_token)
        logger.info("KiteConnect session established successfully")
    
    @property
    def is_configured(self) -> bool:
        """Check if client is configured with credentials."""
        return bool(self.api_key and self.access_token)
    
    @property
    def kite(self) -> "KiteConnect":
        """Get the KiteConnect instance.
        
        Raises:
            KiteClientError: If credentials not configured
        """
        if not self.is_configured:
            raise KiteClientError(
                "Kite credentials not configured. "
                "Set KITE_API_KEY and either KITE_ACCESS_TOKEN or KITE_TOKEN_URL."
            )
        
        if self._kite is None:
            self._init_kite()
        
        return self._kite  # type: ignore
    
    def get_margins(self, segment: str = "equity") -> dict[str, Any]:
        """Get available margins.
        
        Args:
            segment: 'equity' or 'commodity'
        
        Returns:
            Margin data from Kite
        """
        try:
            return self.kite.margins(segment=segment)
        except Exception as e:
            raise KiteClientError(f"Failed to get margins: {e}") from e
    
    def order_margins(self, orders: list[dict[str, Any]]) -> list[dict[str, Any]]:
        """Get margin requirements for orders.
        
        Args:
            orders: List of order parameters with keys:
                - exchange: NFO, NSE, BSE, etc.
                - tradingsymbol: Trading symbol
                - transaction_type: BUY or SELL
                - quantity: Order quantity
                - product: NRML, MIS, or CNC
                - order_type: MARKET or LIMIT
                - price: Price (for LIMIT orders)
        
        Returns:
            List of margin requirements per order
        """
        try:
            return self.kite.order_margins(orders)
        except Exception as e:
            raise KiteClientError(f"Failed to get order margins: {e}") from e
    
    def basket_margins(
        self,
        orders: list[dict[str, Any]],
        consider_positions: bool = False,
    ) -> dict[str, Any]:
        """Get combined margin for a basket of orders.
        
        Args:
            orders: List of order parameters
            consider_positions: Whether to consider existing positions
        
        Returns:
            Combined basket margin data with benefit from hedging
        """
        try:
            # Try the SDK's basket_order_margins method first (newer SDK versions)
            if hasattr(self.kite, 'basket_order_margins'):
                return self.kite.basket_order_margins(
                    orders, 
                    consider_positions=consider_positions,
                    mode=None
                )
            
            # Fallback: Use _post with the correct route
            # Route: order.margins.basket -> /margins/basket
            return self.kite._post(
                "order.margins.basket",
                params=orders,
                is_json=True,
                query_params={
                    "consider_positions": consider_positions,
                    "mode": None
                }
            )
                
        except Exception as e:
            raise KiteClientError(f"Failed to get basket margins: {e}") from e
    
    def get_order_charges(self, orders: list[dict[str, Any]]) -> list[dict[str, Any]]:
        """Get charges for orders (if Kite charges endpoint is available).
        
        Args:
            orders: List of order parameters
        
        Returns:
            Charges breakdown per order
        """
        try:
            return self.kite._post("charges/orders", params={"orders": orders})  # type: ignore
        except Exception as e:
            raise KiteClientError(f"Charges endpoint not available or failed: {e}") from e
    
    def historical_data(
        self,
        instrument_token: int,
        from_date: str,
        to_date: str,
        interval: str = "day",
    ) -> list[dict[str, Any]]:
        """Get historical OHLCV data.
        
        Args:
            instrument_token: Instrument token
            from_date: Start date (YYYY-MM-DD)
            to_date: End date (YYYY-MM-DD)
            interval: Candle interval (minute, day, etc.)
        
        Returns:
            List of OHLCV candles
        """
        try:
            return self.kite.historical_data(
                instrument_token, from_date, to_date, interval=interval
            )
        except Exception as e:
            raise KiteClientError(f"Failed to get historical data: {e}") from e
    
    def instruments(self, exchange: str | None = None) -> list[dict[str, Any]]:
        """Get list of tradeable instruments.
        
        Args:
            exchange: Optional exchange filter (NFO, NSE, BSE, BFO, CDS)
        
        Returns:
            List of instruments
        """
        try:
            return self.kite.instruments(exchange=exchange)
        except Exception as e:
            raise KiteClientError(f"Failed to get instruments: {e}") from e


# ─────────────────────────────────────────────────────────────────────────────────
# Singleton instance - created at backend startup
# ─────────────────────────────────────────────────────────────────────────────────

_kite_client: KiteClient | None = None
_kite_client_initialized: bool = False


def init_kite_client() -> KiteClient | None:
    """Initialize the global Kite client singleton.
    
    Should be called once at backend startup.
    
    Returns:
        KiteClient instance if credentials are configured, None otherwise
    """
    global _kite_client, _kite_client_initialized
    
    if _kite_client_initialized:
        return _kite_client
    
    _kite_client_initialized = True
    
    if not KITE_AVAILABLE:
        logger.warning("kiteconnect package not installed. Kite features disabled.")
        return None
    
    settings = get_settings()
    
    # Check if we have minimum required credentials
    if not settings.kite_api_key:
        logger.info("KITE_API_KEY not set. Kite features disabled.")
        return None
    
    if not settings.kite_access_token and not settings.kite_token_url:
        logger.info(
            "Neither KITE_ACCESS_TOKEN nor KITE_TOKEN_URL set. Kite features disabled."
        )
        return None
    
    try:
        _kite_client = KiteClient()
        logger.info("Kite client initialized successfully")
        return _kite_client
    except (KiteClientError, CredentialError) as e:
        logger.error(f"Failed to initialize Kite client: {e}")
        _kite_client = None
        return None


def get_kite_client() -> KiteClient | None:
    """Get the global Kite client singleton.
    
    Returns:
        KiteClient instance if configured and initialized, None otherwise
    """
    global _kite_client, _kite_client_initialized
    
    if not _kite_client_initialized:
        return init_kite_client()
    
    return _kite_client


def get_kite_client_or_raise() -> KiteClient:
    """Get the global Kite client or raise if not available.
    
    Returns:
        KiteClient instance
        
    Raises:
        KiteClientError: If Kite client is not configured/available
    """
    client = get_kite_client()
    if client is None:
        raise KiteClientError(
            "Kite client not available. Configure KITE_API_KEY and "
            "either KITE_ACCESS_TOKEN or KITE_TOKEN_URL in environment."
        )
    return client
