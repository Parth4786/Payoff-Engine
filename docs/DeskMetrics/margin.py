"""Margin API endpoints for basket margin calculation."""
from __future__ import annotations

import uuid
from typing import Any

from fastapi import APIRouter, Depends, HTTPException, UploadFile, File
from pydantic import BaseModel, Field

from backend.app.api.deps import get_db
from backend.app.infra.kite_provider import get_kite_provider, KiteProvider
from backend.app.domain.instruments.service import InstrumentService


router = APIRouter(prefix="/margin", tags=["margin"])

# In-memory storage for CSV uploads (in production, use Redis or DB)
_csv_uploads: dict[str, dict] = {}


class MarginOrder(BaseModel):
    """Order for margin calculation."""
    exchange: str
    tradingsymbol: str
    transaction_type: str = Field(description="BUY or SELL")
    quantity: int
    product: str = Field(description="MIS, NRML, or CNC")
    order_type: str = Field(default="MARKET", description="MARKET or LIMIT")
    price: float | None = None
    trigger_price: float | None = None
    variety: str | None = None


class BasketMarginRequest(BaseModel):
    """Request for basket margin calculation."""
    orders: list[MarginOrder]


class MarginBreakdown(BaseModel):
    """Margin breakdown."""
    total: float
    span: float
    exposure: float
    option_premium: float
    additional: float | None = None
    var: float | None = None


class ChargeItem(BaseModel):
    """Single charge item."""
    name: str
    amount: float


class ChargeBreakdown(BaseModel):
    """Charges breakdown."""
    total: float
    breakdown: list[ChargeItem]


class PerLegMargin(BaseModel):
    """Per-leg margin info."""
    tradingsymbol: str
    margin: float


class BasketMarginResponse(BaseModel):
    """Response for basket margin calculation."""
    initial: MarginBreakdown
    final: MarginBreakdown
    charges: ChargeBreakdown | None = None
    source: str = Field(description="kite or manual")
    per_leg: list[PerLegMargin] | None = None


class CsvUploadResponse(BaseModel):
    """Response for CSV upload."""
    upload_id: str
    row_count: int
    columns: list[str]
    sample_rows: list[dict[str, str]]


class CsvColumnMapping(BaseModel):
    """Single column mapping."""
    csvColumn: str
    internalField: str | None
    transform: str | None = None


class CsvMapRequest(BaseModel):
    """Request to map CSV columns."""
    upload_id: str
    mappings: list[CsvColumnMapping]


class ResolutionResult(BaseModel):
    """Symbol resolution result."""
    input_symbol: str
    status: str  # matched, partial, unmatched
    instrument: dict | None = None
    confidence: float
    suggestions: list[dict] | None = None


class ResolutionSummary(BaseModel):
    """Resolution summary."""
    matched: int
    partial: int
    unmatched: int
    total: int


class BulkResolveResponse(BaseModel):
    """Bulk resolution response."""
    results: list[ResolutionResult]
    summary: ResolutionSummary


class StrategyLeg(BaseModel):
    """Strategy leg."""
    id: str
    instrument: dict
    side: str
    quantity: int
    product: str
    entryPrice: float | None = None
    locked: bool | None = None


class CsvRowError(BaseModel):
    """CSV row error."""
    row: int
    message: str


class CsvMapResponse(BaseModel):
    """Response for CSV mapping."""
    resolution: BulkResolveResponse
    legs: list[StrategyLeg]
    errors: list[CsvRowError]


@router.post("/basket", response_model=BasketMarginResponse)
async def calculate_basket_margin(
    body: BasketMarginRequest,
    kite_provider: KiteProvider = Depends(get_kite_provider),
) -> BasketMarginResponse:
    """Calculate margin for a basket of orders."""
    
    # Helper to calculate estimated margins for fallback
    def calculate_fallback_margin(orders: list[MarginOrder]) -> BasketMarginResponse:
        """Calculate estimated margins when Kite API is not available.
        
        This is a rough approximation for testing/demo purposes.
        Real margins should come from Kite API.
        """
        total_margin = 0
        total_span = 0
        total_exposure = 0
        total_premium = 0
        per_leg = []
        
        for o in orders:
            # Estimate margin based on product and instrument type
            qty = o.quantity
            
            # Detect if it's an option (CE/PE suffix)
            is_option = any(o.tradingsymbol.endswith(suffix) for suffix in ["CE", "PE"])
            is_future = "FUT" in o.tradingsymbol
            is_banknifty = "BANKNIFTY" in o.tradingsymbol
            is_nifty = "NIFTY" in o.tradingsymbol and not is_banknifty
            
            # Lot sizes
            lot_size = 35 if is_banknifty else 75 if is_nifty else 50
            lots = max(1, qty // lot_size) if qty > 0 else 1
            
            if is_option:
                # Options: ~20% of notional for SELL, premium for BUY
                if o.transaction_type == "SELL":
                    # Rough margin per lot: ~100k for BANKNIFTY, ~80k for NIFTY
                    estimated = lots * (100000 if is_banknifty else 80000)
                    total_span += estimated * 0.6
                    total_exposure += estimated * 0.4
                else:
                    # Just premium for buying - rough ~5k per lot
                    estimated = lots * 5000
                    total_premium += estimated
                total_margin += estimated
            elif is_future:
                # Futures: ~12-15% of notional
                # NIFTY lot ~23900 * 75 = 1.8M notional, margin ~2.2L
                # BANKNIFTY lot ~51500 * 35 = 1.8M notional, margin ~2.4L
                if is_banknifty:
                    estimated = lots * 240000  # ~2.4L per lot
                elif is_nifty:
                    estimated = lots * 170000  # ~1.7L per lot
                else:
                    estimated = lots * 150000  # Generic future
                total_span += estimated * 0.7
                total_exposure += estimated * 0.3
                total_margin += estimated
            else:
                # Equity - assume ~1500 per share for intraday
                estimated = qty * 1500
                total_margin += estimated
            
            per_leg.append(PerLegMargin(tradingsymbol=o.tradingsymbol, margin=estimated))
        
        # For spreads, apply margin benefit (rough estimate: 60% reduction)
        buys = [o for o in orders if o.transaction_type == "BUY"]
        sells = [o for o in orders if o.transaction_type == "SELL"]
        if buys and sells:
            total_margin = total_margin * 0.4
            total_span = total_span * 0.4
            total_exposure = total_exposure * 0.4
        
        return BasketMarginResponse(
            initial=MarginBreakdown(
                total=total_margin,
                span=total_span,
                exposure=total_exposure,
                option_premium=total_premium,
            ),
            final=MarginBreakdown(
                total=total_margin,
                span=total_span,
                exposure=total_exposure,
                option_premium=total_premium,
            ),
            source="estimated",
            per_leg=per_leg,
        )
    
    # Check if Kite is available
    if not kite_provider.is_available:
        return calculate_fallback_margin(body.orders)
    
    try:
        kite = kite_provider.client
        if not kite:
            return calculate_fallback_margin(body.orders)
        
        # Build orders for Kite API
        orders = []
        for o in body.orders:
            order = {
                "exchange": o.exchange,
                "tradingsymbol": o.tradingsymbol,
                "transaction_type": o.transaction_type,
                "quantity": o.quantity,
                "product": o.product,
                "order_type": o.order_type,
                "variety": o.variety or "regular",
            }
            if o.price:
                order["price"] = o.price
            if o.trigger_price:
                order["trigger_price"] = o.trigger_price
            orders.append(order)
        
        # Call Kite basket margins API
        # Note: mode is not supported as kwarg in Python SDK
        result = kite.basket_margins(orders)
        
        # Extract margins
        initial = result.get("initial", {})
        final = result.get("final", {})
        
        # Build per-leg if available
        per_leg = None
        if "orders" in result:
            per_leg = []
            for i, leg_result in enumerate(result["orders"]):
                per_leg.append(PerLegMargin(
                    tradingsymbol=body.orders[i].tradingsymbol,
                    margin=leg_result.get("total", 0),
                ))
        
        # Extract charges if available
        charges = None
        charges_data = final.get("charges", {})
        if charges_data:
            breakdown = []
            total_charges = 0
            for key, value in charges_data.items():
                if isinstance(value, (int, float)) and value > 0:
                    breakdown.append(ChargeItem(name=key, amount=value))
                    total_charges += value
            if breakdown:
                charges = ChargeBreakdown(total=total_charges, breakdown=breakdown)
        
        return BasketMarginResponse(
            initial=MarginBreakdown(
                total=initial.get("total", 0),
                span=initial.get("span", 0),
                exposure=initial.get("exposure", 0),
                option_premium=initial.get("option_premium", 0),
                additional=initial.get("additional"),
                var=initial.get("var"),
            ),
            final=MarginBreakdown(
                total=final.get("total", 0),
                span=final.get("span", 0),
                exposure=final.get("exposure", 0),
                option_premium=final.get("option_premium", 0),
                additional=final.get("additional"),
                var=final.get("var"),
            ),
            charges=charges,
            source="kite",
            per_leg=per_leg,
        )
    
    except Exception as e:
        # Log the error but return fallback estimate instead of failing
        import logging
        logging.getLogger(__name__).warning(f"Kite margin API failed, using fallback: {e}")
        return calculate_fallback_margin(body.orders)


@router.post("/csv-upload", response_model=CsvUploadResponse)
async def upload_csv(file: UploadFile = File(...)) -> CsvUploadResponse:
    """Upload CSV file for margin calculation."""
    import csv
    import io
    
    if not file.filename or not file.filename.endswith('.csv'):
        raise HTTPException(status_code=400, detail="File must be a CSV")
    
    content = await file.read()
    
    # Check size
    if len(content) > 10 * 1024 * 1024:  # 10MB
        raise HTTPException(status_code=400, detail="File too large (max 10MB)")
    
    try:
        text = content.decode('utf-8')
        reader = csv.DictReader(io.StringIO(text))
        rows = list(reader)
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"Failed to parse CSV: {str(e)}")
    
    if not rows:
        raise HTTPException(status_code=400, detail="CSV file is empty")
    
    columns = list(rows[0].keys()) if rows else []
    
    # Generate upload ID and store
    upload_id = str(uuid.uuid4())
    _csv_uploads[upload_id] = {
        "rows": rows,
        "columns": columns,
        "filename": file.filename,
    }
    
    return CsvUploadResponse(
        upload_id=upload_id,
        row_count=len(rows),
        columns=columns,
        sample_rows=rows[:5],
    )


@router.post("/csv-map", response_model=CsvMapResponse)
async def map_csv_columns(
    body: CsvMapRequest,
    kite_provider: KiteProvider = Depends(get_kite_provider),
) -> CsvMapResponse:
    """Map CSV columns and resolve symbols to instruments."""
    
    # Get upload data
    upload_data = _csv_uploads.get(body.upload_id)
    if not upload_data:
        raise HTTPException(status_code=404, detail="Upload not found")
    
    rows = upload_data["rows"]
    
    # Build field mapping
    field_map: dict[str, str] = {}
    for m in body.mappings:
        if m.internalField:
            field_map[m.internalField] = m.csvColumn
    
    # Required fields
    if "symbol" not in field_map:
        raise HTTPException(status_code=400, detail="Symbol mapping required")
    if "side" not in field_map:
        raise HTTPException(status_code=400, detail="Side mapping required")
    if "quantity" not in field_map:
        raise HTTPException(status_code=400, detail="Quantity mapping required")
    
    # Get instrument service
    instrument_service = InstrumentService()
    
    # Process rows
    symbols = []
    errors: list[CsvRowError] = []
    
    for i, row in enumerate(rows):
        symbol = row.get(field_map["symbol"], "").strip()
        if not symbol:
            errors.append(CsvRowError(row=i + 1, message="Missing symbol"))
        else:
            symbols.append(symbol)
    
    # Resolve symbols
    resolution_results = await instrument_service.resolve_symbols_bulk(symbols)
    
    # Build legs
    legs: list[StrategyLeg] = []
    full_results: list[ResolutionResult] = []
    matched = 0
    partial = 0
    unmatched = 0
    
    for i, (row, result) in enumerate(zip(rows, resolution_results)):
        # Skip rows with errors
        symbol = row.get(field_map["symbol"], "").strip()
        if not symbol:
            continue
        
        instrument = result.get("instrument")
        status = result.get("status", "unmatched")
        
        full_results.append(ResolutionResult(
            input_symbol=symbol,
            status=status,
            instrument=instrument,
            confidence=result.get("confidence", 0),
            suggestions=result.get("suggestions"),
        ))
        
        if status == "matched":
            matched += 1
        elif status == "partial":
            partial += 1
        else:
            unmatched += 1
        
        if instrument:
            # Parse other fields
            side_raw = row.get(field_map["side"], "BUY").strip().upper()
            side = "BUY" if side_raw in ("BUY", "B", "LONG", "L") else "SELL"
            
            qty_raw = row.get(field_map["quantity"], "0").strip()
            try:
                quantity = int(float(qty_raw))
            except:
                quantity = instrument.get("lot_size", 1)
            
            product = "NRML"
            if "product" in field_map:
                prod_raw = row.get(field_map["product"], "NRML").strip().upper()
                product = prod_raw if prod_raw in ("MIS", "NRML", "CNC") else "NRML"
            
            price = None
            if "price" in field_map:
                try:
                    price = float(row.get(field_map["price"], "0"))
                except:
                    pass
            
            legs.append(StrategyLeg(
                id=str(uuid.uuid4()),
                instrument=instrument,
                side=side,
                quantity=quantity,
                product=product,
                entryPrice=price,
                locked=True,
            ))
    
    return CsvMapResponse(
        resolution=BulkResolveResponse(
            results=full_results,
            summary=ResolutionSummary(
                matched=matched,
                partial=partial,
                unmatched=unmatched,
                total=len(full_results),
            ),
        ),
        legs=legs,
        errors=errors,
    )


class BuildSymbolRequest(BaseModel):
    """Request to build a tradingsymbol."""
    underlying: str = Field(description="Base symbol e.g., NIFTY, BANKNIFTY, RELIANCE")
    expiry: str = Field(description="Expiry date in YYYY-MM-DD format")
    instrument_type: str = Field(description="FUT, CE, or PE")
    strike: float | None = Field(default=None, description="Strike price (required for CE/PE)")
    exchange: str = Field(default="NFO", description="Exchange (NFO, BFO, etc.)")


class BuildSymbolResponse(BaseModel):
    """Response for symbol building."""
    success: bool
    built_symbol: str | None
    instrument: dict | None
    message: str
    suggestions: list[dict]


@router.post("/build-symbol", response_model=BuildSymbolResponse)
async def build_tradingsymbol(
    body: BuildSymbolRequest,
) -> BuildSymbolResponse:
    """Build a Kite tradingsymbol from components and validate it exists.
    
    This is useful when CSV has separate columns for underlying, expiry, strike, etc.
    instead of a combined tradingsymbol.
    
    Examples:
    - NIFTY weekly option: underlying=NIFTY, expiry=2025-05-08, instrument_type=CE, strike=24350
    - BANKNIFTY monthly: underlying=BANKNIFTY, expiry=2025-05-29, instrument_type=FUT
    - Stock option: underlying=RELIANCE, expiry=2025-05-29, instrument_type=CE, strike=1410
    """
    instrument_service = InstrumentService()
    
    result = instrument_service.build_and_validate_symbol(
        underlying=body.underlying,
        expiry=body.expiry,
        instrument_type=body.instrument_type,
        strike=body.strike,
        exchange=body.exchange,
    )
    
    return BuildSymbolResponse(
        success=result["success"],
        built_symbol=result["built_symbol"],
        instrument=result["instrument"],
        message=result["message"],
        suggestions=result["suggestions"],
    )


class CsvMapV2Request(BaseModel):
    """Request to map CSV columns with component-based symbol building."""
    upload_id: str
    mappings: list[CsvColumnMapping]
    use_symbol_builder: bool = Field(
        default=False, 
        description="If true, build symbols from components (underlying, expiry, strike, type) instead of direct tradingsymbol"
    )


@router.post("/csv-map-v2", response_model=CsvMapResponse)
async def map_csv_columns_v2(
    body: CsvMapV2Request,
    kite_provider: KiteProvider = Depends(get_kite_provider),
) -> CsvMapResponse:
    """Map CSV columns and resolve symbols - supports component-based building.
    
    If use_symbol_builder=True, requires mappings for: underlying, expiry, instrument_type, strike (optional for FUT)
    If use_symbol_builder=False, requires mapping for: symbol (existing behavior)
    """
    
    # Get upload data
    upload_data = _csv_uploads.get(body.upload_id)
    if not upload_data:
        raise HTTPException(status_code=404, detail="Upload not found")
    
    rows = upload_data["rows"]
    
    # Build field mapping
    field_map: dict[str, str] = {}
    for m in body.mappings:
        if m.internalField:
            field_map[m.internalField] = m.csvColumn
    
    # Validate required fields based on mode
    if body.use_symbol_builder:
        required = ["underlying", "expiry", "instrument_type", "side", "quantity"]
        for req in required:
            if req not in field_map:
                raise HTTPException(status_code=400, detail=f"{req} mapping required for symbol builder mode")
    else:
        if "symbol" not in field_map:
            raise HTTPException(status_code=400, detail="Symbol mapping required")
        if "side" not in field_map:
            raise HTTPException(status_code=400, detail="Side mapping required")
        if "quantity" not in field_map:
            raise HTTPException(status_code=400, detail="Quantity mapping required")
    
    # Get instrument service
    instrument_service = InstrumentService()
    
    # Process rows
    errors: list[CsvRowError] = []
    full_results: list[ResolutionResult] = []
    legs: list[StrategyLeg] = []
    matched = 0
    partial = 0
    unmatched = 0
    
    for i, row in enumerate(rows):
        # Extract side and quantity first (needed for both modes)
        side_raw = row.get(field_map.get("side", ""), "BUY").strip().upper()
        side = "BUY" if side_raw in ("BUY", "B", "LONG", "L") else "SELL"
        
        qty_raw = row.get(field_map.get("quantity", ""), "0").strip()
        try:
            quantity = int(float(qty_raw))
        except:
            quantity = 0
        
        product = "NRML"
        if "product" in field_map:
            prod_raw = row.get(field_map["product"], "NRML").strip().upper()
            product = prod_raw if prod_raw in ("MIS", "NRML", "CNC") else "NRML"
        
        exchange = "NFO"
        if "exchange" in field_map:
            exchange = row.get(field_map["exchange"], "NFO").strip().upper()
        
        if body.use_symbol_builder:
            # Build symbol from components
            underlying = row.get(field_map["underlying"], "").strip().upper()
            expiry = row.get(field_map["expiry"], "").strip()
            instr_type = row.get(field_map["instrument_type"], "").strip().upper()
            
            strike = None
            if "strike" in field_map:
                strike_raw = row.get(field_map["strike"], "").strip()
                if strike_raw:
                    try:
                        strike = float(strike_raw)
                    except:
                        errors.append(CsvRowError(row=i + 1, message=f"Invalid strike: {strike_raw}"))
                        continue
            
            if not underlying:
                errors.append(CsvRowError(row=i + 1, message="Missing underlying"))
                continue
            if not expiry:
                errors.append(CsvRowError(row=i + 1, message="Missing expiry"))
                continue
            if not instr_type:
                errors.append(CsvRowError(row=i + 1, message="Missing instrument_type"))
                continue
            
            # Normalize instrument type
            if instr_type in ("CALL", "C"):
                instr_type = "CE"
            elif instr_type in ("PUT", "P"):
                instr_type = "PE"
            elif instr_type in ("FUTURE", "F"):
                instr_type = "FUT"
            
            if instr_type in ("CE", "PE") and strike is None:
                errors.append(CsvRowError(row=i + 1, message=f"Strike required for {instr_type}"))
                continue
            
            # Build and validate symbol
            result = instrument_service.build_and_validate_symbol(
                underlying=underlying,
                expiry=expiry,
                instrument_type=instr_type,
                strike=strike,
                exchange=exchange,
            )
            
            input_symbol = f"{underlying} {expiry} {strike or ''} {instr_type}".strip()
            
            if result["success"] and result["instrument"]:
                full_results.append(ResolutionResult(
                    input_symbol=input_symbol,
                    status="matched",
                    instrument=result["instrument"],
                    confidence=1.0,
                    suggestions=[],
                ))
                matched += 1
                
                instrument = result["instrument"]
                if quantity == 0:
                    quantity = instrument.get("lot_size", 1)
                
                legs.append(StrategyLeg(
                    id=str(uuid.uuid4()),
                    instrument=instrument,
                    side=side,
                    quantity=quantity,
                    product=product,
                    entryPrice=None,
                    locked=True,
                ))
            else:
                full_results.append(ResolutionResult(
                    input_symbol=input_symbol,
                    status="unmatched",
                    instrument=None,
                    confidence=0,
                    suggestions=result.get("suggestions", []),
                ))
                unmatched += 1
        else:
            # Original direct symbol resolution
            symbol = row.get(field_map["symbol"], "").strip()
            if not symbol:
                errors.append(CsvRowError(row=i + 1, message="Missing symbol"))
                continue
            
            # Resolve symbol
            resolution = await instrument_service.resolve_symbols_bulk([symbol])
            result = resolution[0] if resolution else {"status": "unmatched", "instrument": None, "confidence": 0, "suggestions": []}
            
            instrument = result.get("instrument")
            status = result.get("status", "unmatched")
            
            full_results.append(ResolutionResult(
                input_symbol=symbol,
                status=status,
                instrument=instrument,
                confidence=result.get("confidence", 0),
                suggestions=result.get("suggestions"),
            ))
            
            if status == "matched":
                matched += 1
            elif status == "partial":
                partial += 1
            else:
                unmatched += 1
            
            if instrument:
                if quantity == 0:
                    quantity = instrument.get("lot_size", 1)
                
                price = None
                if "price" in field_map:
                    try:
                        price = float(row.get(field_map["price"], "0"))
                    except:
                        pass
                
                legs.append(StrategyLeg(
                    id=str(uuid.uuid4()),
                    instrument=instrument,
                    side=side,
                    quantity=quantity,
                    product=product,
                    entryPrice=price,
                    locked=True,
                ))
    
    return CsvMapResponse(
        resolution=BulkResolveResponse(
            results=full_results,
            summary=ResolutionSummary(
                matched=matched,
                partial=partial,
                unmatched=unmatched,
                total=len(full_results),
            ),
        ),
        legs=legs,
        errors=errors,
    )
