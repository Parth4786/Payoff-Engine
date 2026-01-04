from __future__ import annotations

from pathlib import Path
from typing import List

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )

    # Storage
    file_store_path: str = "./.data/files"

    # Database
    database_url: str = "sqlite:///./.data/deskmetrics.sqlite3"

    # CORS
    cors_origins: List[str] = ["http://localhost:5173"]

    # Kite (server-side only; optional in v1 scaffolding)
    kite_api_key: str | None = None
    kite_api_secret: str | None = None  # Not needed if using token URL
    kite_access_token: str | None = None  # Can be set directly OR fetched from URL
    kite_token_url: str | None = None  # URL to fetch access token (e.g., http://110.172.21.62:5005/token/zerodha)

    # ─────────────────────────────────────────────────────────────────────
    # Annualization Constants
    # NOTE: These are NOT 252/365 - they are specific to your trading calendar
    # ─────────────────────────────────────────────────────────────────────
    # Number of trading days per year for discrete return annualization
    # Example: For NSE with holidays removed, this might be ~240-245
    annualization_discrete_days: int = 240
    
    # Number of days per year for continuous return annualization
    # Example: For continuous compounding, this might be ~365 or your specific count
    annualization_continuous_days: int = 365
    
    # ─────────────────────────────────────────────────────────────────────
    # Interest Rates (as decimals, e.g., 0.12 = 12%)
    # Based on desk expense calculator rates
    # ─────────────────────────────────────────────────────────────────────
    # Intraday margin funding rate (annualized) - 6%
    interest_rate_intraday: float = 0.06
    
    # Overnight/positional margin funding rate (annualized) - 12%
    interest_rate_overnight: float = 0.12
    
    # Risk-free rate for Sharpe ratio calculations (annualized)
    risk_free_rate: float = 0.065

    # ─────────────────────────────────────────────────────────────────────
    # Trading Calendar
    # ─────────────────────────────────────────────────────────────────────
    trading_calendar: str = "mon_fri_no_holidays"
    
    # ─────────────────────────────────────────────────────────────────────
    # Currency
    # ─────────────────────────────────────────────────────────────────────
    currency_code: str = "INR"
    currency_decimals: int = 2

    def ensure_data_dirs(self) -> None:
        Path(self.file_store_path).mkdir(parents=True, exist_ok=True)
        if self.database_url.startswith("sqlite:///./"):
            db_path = self.database_url.removeprefix("sqlite:///./")
            Path(db_path).parent.mkdir(parents=True, exist_ok=True)


_settings: Settings | None = None


def get_settings() -> Settings:
    global _settings
    if _settings is None:
        _settings = Settings()
        _settings.ensure_data_dirs()
    return _settings
