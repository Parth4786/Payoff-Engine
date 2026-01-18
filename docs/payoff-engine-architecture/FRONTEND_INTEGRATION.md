# Payoff Engine - Frontend Integration Guide

> **Purpose**: Practical guide for frontend developers to integrate with the Payoff Engine backend.

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [TypeScript Types](#2-typescript-types)
3. [API Client Setup](#3-api-client-setup)
4. [Component Integration Patterns](#4-component-integration-patterns)
5. [State Management](#5-state-management)
6. [Error Handling](#6-error-handling)
7. [Performance Optimization](#7-performance-optimization)
8. [Common Pitfalls](#8-common-pitfalls)

---

## 1. Quick Start

### 1.1 Basic Setup

```bash
# Backend must be running
cd cpp/build
./payoff_engine --port 8080

# Frontend connects to
API_BASE_URL=http://localhost:8080
WS_URL=ws://localhost:8080/ws
```

### 1.2 First API Call

```typescript
// Fetch health status
const response = await fetch('http://localhost:8080/api/health');
const data = await response.json();

console.log(data);
// { status: "ok", timestamp: "1767521472123", version: "1.0.0" }
```

### 1.3 Calculate a Simple Payoff

```typescript
const response = await fetch('http://localhost:8080/api/payoff/calculate', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    underlying: 'NIFTY',
    spot: 26300,
    legs: [
      { type: 'CE', side: 'BUY', strike: 26300, qty: 1, lot: 25, premium: 250 },
      { type: 'CE', side: 'SELL', strike: 26500, qty: 1, lot: 25, premium: 150 }
    ]
  })
});

const payoff = await response.json();
console.log(payoff.max_profit, payoff.max_loss, payoff.breakevens);
```

---

## 2. TypeScript Types

Create a `types/payoff-engine.ts` file:

```typescript
// ============================================================================
// Core Market Data
// ============================================================================

export interface DepthLevel {
  price: number;
  size: number;
  orders: number;
}

export interface TradeInfo {
  last_price: number;
  last_qty: number;
  total_traded_quantity: number;
  average_traded_price: number;
  total_buy_quantity: number;
  total_sell_quantity: number;
  oi: number | null;
  oi_day_high: number | null;
  oi_day_low: number | null;
  last_trade_time_s: number | null;
  exchange_timestamp_s: number | null;
  ohlc: {
    open: number;
    high: number;
    low: number;
    close: number;
  };
}

export type Source = 'clickhouse' | 'kite_ws' | 'mock';

export interface DepthSnapshot {
  type: 'depth_snapshot';
  symbol: string;
  instrument_id: number;
  capture_time_ms: number;
  bids: DepthLevel[];
  asks: DepthLevel[];
  trade: TradeInfo;
  source: Source;
  is_partial: boolean;
  is_stale: boolean;
}

// ============================================================================
// Option Strategy
// ============================================================================

export type OptionType = 'CE' | 'PE';
export type Side = 'BUY' | 'SELL';

export interface OptionLeg {
  type: OptionType;
  side: Side;
  strike: number;
  expiry?: string;
  quantity: number;
  lot_size: number;
  premium: number;
  instrument_id?: number;
  symbol?: string;
  // Computed by server
  total_qty?: number;
  net_premium?: number;
}

export interface Strategy {
  name: string;
  underlying: string;
  underlying_price: number;
  legs: OptionLeg[];
  total_premium?: number;
  is_credit?: boolean;
  is_debit?: boolean;
  leg_count?: number;
}

export interface Scenario {
  spot_shift_pct: number;
  iv_shift_pct: number;
  days_forward: number;
  description: string;
}

// ============================================================================
// Payoff & Greeks
// ============================================================================

export interface Greeks {
  delta: number;
  gamma: number;
  theta: number;
  vega: number;
  rho: number;
}

export interface PayoffPoint {
  spot: number;
  pnl: number;
  pnl_pct?: number;
  greeks?: Greeks;
}

export interface PayoffCurve {
  scenario_name: string;
  points: PayoffPoint[];
  max_profit: number;
  max_loss: number;
  breakevens: number[];
  probability_of_profit: number;
  expected_value: number;
  tail_loss_5pct: number;
  tail_loss_1pct: number;
  greeks: Greeks;
}

// ============================================================================
// Screener
// ============================================================================

export interface InstrumentSnapshot {
  instrument_id: number;
  tradingsymbol: string;
  underlying: string;
  exchange: string;
  instrument_type: number;
  strike: number;
  option_type: OptionType;
  expiry_ms: number;
  days_to_expiry: number;
  last_price: number;
  bid_price: number;
  ask_price: number;
  mid_price: number;
  spread: number;
  spread_pct: number;
  volume: number;
  open_interest: number;
  oi_change: number;
  iv: number;
  iv_pct: number;
  iv_percentile: number;
  delta: number;
  gamma: number;
  theta: number;
  vega: number;
  moneyness: number;
  is_itm: boolean;
  is_atm: boolean;
  is_otm: boolean;
  timestamp: number;
}

export interface UnderlyingSnapshot {
  symbol: string;
  spot_price: number;
  prev_close: number;
  change: number;
  change_pct: number;
  day_high: number;
  day_low: number;
  volume: number;
  atm_iv: number;
  atm_iv_percentile: number;
  total_calls: number;
  total_puts: number;
  total_call_oi: number;
  total_put_oi: number;
  pcr_oi: number;
  pcr_volume: number;
}

export interface MarketScreenerResult {
  timestamp: number;
  underlyings: UnderlyingSnapshot[];
  instruments: InstrumentSnapshot[];
  total_instruments: number;
  options_count: number;
  futures_count: number;
  equities_count: number;
  query_time_ms: number;
  calc_time_ms: number;
}

export interface ScreenerFilter {
  exchanges?: string[];
  underlyings?: string[];
  include_options?: boolean;
  include_futures?: boolean;
  include_equities?: boolean;
  include_calls?: boolean;
  include_puts?: boolean;
  min_dte?: number;
  max_dte?: number;
  specific_expiry_ms?: number;
  only_itm?: boolean;
  only_atm?: boolean;
  only_otm?: boolean;
  min_volume?: number;
  min_oi?: number;
  min_iv?: number;
  max_iv?: number;
  min_delta?: number;
  max_delta?: number;
  max_spread_pct?: number;
  offset?: number;
  limit?: number;
  sort_by?: string;
  sort_order?: 'asc' | 'desc';
}

// ============================================================================
// Execution Hint
// ============================================================================

export type Posture = 'WAIT' | 'PASSIVE' | 'AGGRESSIVE';

export interface ExecutionHint {
  posture: Posture;
  reasons: string[];
  confidence: number;
  spread_bps: number;
  imbalance: number;
  depth_slope: number;
  shock: number;
}
```

---

## 3. API Client Setup

### 3.1 Base Client Class

```typescript
// lib/payoff-client.ts

const API_BASE = process.env.NEXT_PUBLIC_API_URL || 'http://localhost:8080';

export class PayoffClient {
  private baseUrl: string;

  constructor(baseUrl: string = API_BASE) {
    this.baseUrl = baseUrl;
  }

  private async request<T>(
    endpoint: string,
    options: RequestInit = {}
  ): Promise<T> {
    const response = await fetch(`${this.baseUrl}${endpoint}`, {
      headers: {
        'Content-Type': 'application/json',
        ...options.headers,
      },
      ...options,
    });

    if (!response.ok) {
      const error = await response.json().catch(() => ({}));
      throw new PayoffApiError(
        error.error || 'Request failed',
        response.status,
        error.code
      );
    }

    return response.json();
  }

  // ========================================================================
  // Payoff Endpoints
  // ========================================================================

  async calculatePayoff(
    underlying: string,
    spot: number,
    legs: OptionLeg[],
    scenario?: Scenario
  ): Promise<PayoffCurve> {
    return this.request('/api/payoff/calculate', {
      method: 'POST',
      body: JSON.stringify({ underlying, spot, legs, scenario }),
    });
  }

  async calculateGreeks(
    spot: number,
    strike: number,
    iv: number,
    dte: number,
    type: OptionType
  ): Promise<{ price: number; greeks: Greeks }> {
    return this.request('/api/greeks/calculate', {
      method: 'POST',
      body: JSON.stringify({ spot, strike, iv, dte, type }),
    });
  }

  async getChainGreeks(params: {
    spot: number;
    iv?: number;
    dte?: number;
    step?: number;
    strikes?: number;
  }) {
    const query = new URLSearchParams(
      Object.entries(params).map(([k, v]) => [k, String(v)])
    );
    return this.request(`/api/chain/greeks?${query}`);
  }

  async calculateIV(
    spot: number,
    strike: number,
    price: number,
    dte: number,
    type: OptionType
  ): Promise<{ iv: number | null; iv_pct: number | null }> {
    return this.request('/api/iv/calculate', {
      method: 'POST',
      body: JSON.stringify({ spot, strike, price, dte, type }),
    });
  }

  // ========================================================================
  // Screener Endpoints
  // ========================================================================

  async getMarketSnapshot(
    timestamp: number,
    filter?: ScreenerFilter
  ): Promise<MarketScreenerResult> {
    const params = new URLSearchParams({ timestamp: String(timestamp) });
    
    if (filter) {
      if (filter.underlyings) params.set('underlyings', filter.underlyings.join(','));
      if (filter.exchanges) params.set('exchanges', filter.exchanges.join(','));
      if (filter.min_dte !== undefined) params.set('min_dte', String(filter.min_dte));
      if (filter.max_dte !== undefined) params.set('max_dte', String(filter.max_dte));
      if (filter.only_atm) params.set('only_atm', 'true');
      if (filter.sort_by) params.set('sort_by', filter.sort_by);
      if (filter.sort_order) params.set('sort_order', filter.sort_order);
      if (filter.limit) params.set('limit', String(filter.limit));
      if (filter.offset) params.set('offset', String(filter.offset));
    }

    return this.request(`/api/screener/market?${params}`);
  }

  async getAvailableExpiries(
    underlying: string,
    asOf?: number
  ): Promise<{ expiries: Array<{ expiry_ms: number; label: string; dte: number }> }> {
    const params = new URLSearchParams({ underlying });
    if (asOf) params.set('as_of', String(asOf));
    return this.request(`/api/screener/expiries?${params}`);
  }

  async getOptionChain(
    underlying: string,
    expiryMs: number,
    timestamp: number
  ) {
    const params = new URLSearchParams({
      underlying,
      expiry_ms: String(expiryMs),
      timestamp: String(timestamp),
    });
    return this.request(`/api/screener/chain?${params}`);
  }
}

// Error class
export class PayoffApiError extends Error {
  constructor(
    message: string,
    public status: number,
    public code?: string
  ) {
    super(message);
    this.name = 'PayoffApiError';
  }
}

// Singleton instance
export const payoffClient = new PayoffClient();
```

---

## 4. Component Integration Patterns

### 4.1 Payoff Chart Component

```tsx
// components/PayoffChart.tsx
import { useQuery } from '@tanstack/react-query';
import { payoffClient } from '@/lib/payoff-client';
import type { OptionLeg, PayoffCurve } from '@/types/payoff-engine';

interface PayoffChartProps {
  underlying: string;
  spot: number;
  legs: OptionLeg[];
}

export function PayoffChart({ underlying, spot, legs }: PayoffChartProps) {
  const { data, isLoading, error } = useQuery({
    queryKey: ['payoff', underlying, spot, legs],
    queryFn: () => payoffClient.calculatePayoff(underlying, spot, legs),
    enabled: legs.length > 0,
    staleTime: 10_000, // 10 seconds
  });

  if (isLoading) return <Skeleton className="h-[400px]" />;
  if (error) return <ErrorDisplay error={error} />;
  if (!data) return null;

  return (
    <div className="space-y-4">
      {/* Summary cards */}
      <div className="grid grid-cols-4 gap-4">
        <StatCard
          label="Max Profit"
          value={formatCurrency(data.max_profit)}
          variant={data.max_profit > 0 ? 'success' : 'default'}
        />
        <StatCard
          label="Max Loss"
          value={formatCurrency(data.max_loss)}
          variant="danger"
        />
        <StatCard
          label="Breakeven"
          value={data.breakevens.map(formatNumber).join(', ') || 'N/A'}
        />
        <StatCard
          label="POP"
          value={formatPercent(data.probability_of_profit)}
        />
      </div>

      {/* Chart */}
      <PayoffLineChart points={data.points} breakevens={data.breakevens} />

      {/* Greeks table */}
      <GreeksDisplay greeks={data.greeks} />
    </div>
  );
}
```

### 4.2 Strategy Builder Component

```tsx
// components/StrategyBuilder.tsx
import { useState, useCallback } from 'react';
import type { OptionLeg, OptionType, Side } from '@/types/payoff-engine';

interface StrategyBuilderProps {
  onLegsChange: (legs: OptionLeg[]) => void;
  lotSize?: number;
}

export function StrategyBuilder({ onLegsChange, lotSize = 25 }: StrategyBuilderProps) {
  const [legs, setLegs] = useState<OptionLeg[]>([]);

  const addLeg = useCallback((leg: Partial<OptionLeg>) => {
    const newLeg: OptionLeg = {
      type: leg.type || 'CE',
      side: leg.side || 'BUY',
      strike: leg.strike || 0,
      quantity: leg.quantity || 1,
      lot_size: lotSize,
      premium: leg.premium || 0,
    };
    
    const updated = [...legs, newLeg];
    setLegs(updated);
    onLegsChange(updated);
  }, [legs, lotSize, onLegsChange]);

  const updateLeg = useCallback((index: number, updates: Partial<OptionLeg>) => {
    const updated = legs.map((leg, i) =>
      i === index ? { ...leg, ...updates } : leg
    );
    setLegs(updated);
    onLegsChange(updated);
  }, [legs, onLegsChange]);

  const removeLeg = useCallback((index: number) => {
    const updated = legs.filter((_, i) => i !== index);
    setLegs(updated);
    onLegsChange(updated);
  }, [legs, onLegsChange]);

  return (
    <div className="space-y-4">
      {/* Leg list */}
      {legs.map((leg, index) => (
        <LegEditor
          key={index}
          leg={leg}
          onChange={(updates) => updateLeg(index, updates)}
          onRemove={() => removeLeg(index)}
        />
      ))}

      {/* Add leg button */}
      <Button onClick={() => addLeg({})}>
        Add Leg
      </Button>

      {/* Quick strategy buttons */}
      <div className="flex gap-2">
        <Button variant="outline" onClick={() => addBullCallSpread()}>
          Bull Call Spread
        </Button>
        <Button variant="outline" onClick={() => addIronCondor()}>
          Iron Condor
        </Button>
        <Button variant="outline" onClick={() => addStraddle()}>
          Straddle
        </Button>
      </div>
    </div>
  );
}
```

### 4.3 Market Screener Component

```tsx
// components/MarketScreener.tsx
import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { payoffClient } from '@/lib/payoff-client';
import type { ScreenerFilter, InstrumentSnapshot } from '@/types/payoff-engine';

interface MarketScreenerProps {
  timestamp: number;
  onInstrumentSelect: (instrument: InstrumentSnapshot) => void;
}

export function MarketScreener({ timestamp, onInstrumentSelect }: MarketScreenerProps) {
  const [filter, setFilter] = useState<ScreenerFilter>({
    underlyings: ['NIFTY'],
    only_atm: false,
    min_dte: 7,
    max_dte: 45,
    sort_by: 'volume',
    sort_order: 'desc',
    limit: 50,
  });

  const { data, isLoading } = useQuery({
    queryKey: ['screener', timestamp, filter],
    queryFn: () => payoffClient.getMarketSnapshot(timestamp, filter),
    refetchInterval: 30_000, // Refresh every 30s
  });

  return (
    <div className="space-y-4">
      {/* Filters */}
      <ScreenerFilters filter={filter} onChange={setFilter} />

      {/* Underlying summary */}
      {data?.underlyings.map((underlying) => (
        <UnderlyingSummaryCard key={underlying.symbol} data={underlying} />
      ))}

      {/* Instruments table */}
      <DataTable
        columns={screenerColumns}
        data={data?.instruments || []}
        isLoading={isLoading}
        onRowClick={onInstrumentSelect}
      />

      {/* Performance stats */}
      {data && (
        <div className="text-sm text-muted-foreground">
          Query: {data.query_time_ms.toFixed(1)}ms |
          Calc: {data.calc_time_ms.toFixed(1)}ms |
          Total: {data.total_instruments} instruments
        </div>
      )}
    </div>
  );
}
```

---

## 5. State Management

### 5.1 Strategy Store (Zustand)

```typescript
// stores/strategy-store.ts
import { create } from 'zustand';
import type { OptionLeg, Strategy, PayoffCurve } from '@/types/payoff-engine';

interface StrategyState {
  // State
  underlying: string;
  spot: number;
  legs: OptionLeg[];
  payoffCurve: PayoffCurve | null;
  isCalculating: boolean;
  error: string | null;

  // Actions
  setUnderlying: (underlying: string) => void;
  setSpot: (spot: number) => void;
  addLeg: (leg: OptionLeg) => void;
  updateLeg: (index: number, updates: Partial<OptionLeg>) => void;
  removeLeg: (index: number) => void;
  clearLegs: () => void;
  setPayoffCurve: (curve: PayoffCurve | null) => void;
  setCalculating: (value: boolean) => void;
  setError: (error: string | null) => void;
}

export const useStrategyStore = create<StrategyState>((set) => ({
  // Initial state
  underlying: 'NIFTY',
  spot: 26300,
  legs: [],
  payoffCurve: null,
  isCalculating: false,
  error: null,

  // Actions
  setUnderlying: (underlying) => set({ underlying }),
  setSpot: (spot) => set({ spot }),
  
  addLeg: (leg) => set((state) => ({
    legs: [...state.legs, leg],
    payoffCurve: null, // Invalidate cached curve
  })),
  
  updateLeg: (index, updates) => set((state) => ({
    legs: state.legs.map((leg, i) =>
      i === index ? { ...leg, ...updates } : leg
    ),
    payoffCurve: null,
  })),
  
  removeLeg: (index) => set((state) => ({
    legs: state.legs.filter((_, i) => i !== index),
    payoffCurve: null,
  })),
  
  clearLegs: () => set({ legs: [], payoffCurve: null }),
  
  setPayoffCurve: (payoffCurve) => set({ payoffCurve }),
  setCalculating: (isCalculating) => set({ isCalculating }),
  setError: (error) => set({ error }),
}));
```

### 5.2 React Query Setup

```typescript
// lib/query-client.ts
import { QueryClient } from '@tanstack/react-query';

export const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      staleTime: 5_000,           // 5 seconds
      gcTime: 5 * 60_000,         // 5 minutes
      retry: 2,
      refetchOnWindowFocus: false,
    },
  },
});
```

---

## 6. Error Handling

### 6.1 Error Boundary

```tsx
// components/ErrorBoundary.tsx
import { Component, ReactNode } from 'react';

interface Props {
  children: ReactNode;
  fallback?: ReactNode;
}

interface State {
  hasError: boolean;
  error?: Error;
}

export class ErrorBoundary extends Component<Props, State> {
  state: State = { hasError: false };

  static getDerivedStateFromError(error: Error): State {
    return { hasError: true, error };
  }

  render() {
    if (this.state.hasError) {
      return this.props.fallback || (
        <div className="p-4 bg-red-50 border border-red-200 rounded">
          <h3 className="font-semibold text-red-800">Something went wrong</h3>
          <p className="text-red-600">{this.state.error?.message}</p>
        </div>
      );
    }

    return this.props.children;
  }
}
```

### 6.2 API Error Handling

```typescript
// lib/error-handler.ts
import { PayoffApiError } from './payoff-client';

export function handleApiError(error: unknown): string {
  if (error instanceof PayoffApiError) {
    switch (error.status) {
      case 400:
        return `Invalid request: ${error.message}`;
      case 404:
        return 'Data not found';
      case 503:
        return 'Service temporarily unavailable';
      default:
        return error.message;
    }
  }
  
  if (error instanceof Error) {
    if (error.message.includes('fetch')) {
      return 'Unable to connect to server. Please check your connection.';
    }
    return error.message;
  }
  
  return 'An unexpected error occurred';
}
```

---

## 7. Performance Optimization

### 7.1 Debounced Updates

```typescript
// hooks/usePayoffCalculation.ts
import { useEffect, useState } from 'react';
import { useDebounce } from 'use-debounce';
import { payoffClient } from '@/lib/payoff-client';

export function usePayoffCalculation(
  underlying: string,
  spot: number,
  legs: OptionLeg[]
) {
  const [debouncedLegs] = useDebounce(legs, 300);
  const [debouncedSpot] = useDebounce(spot, 300);
  
  const { data, isLoading, error } = useQuery({
    queryKey: ['payoff', underlying, debouncedSpot, debouncedLegs],
    queryFn: () => payoffClient.calculatePayoff(underlying, debouncedSpot, debouncedLegs),
    enabled: debouncedLegs.length > 0 && debouncedSpot > 0,
  });

  return { payoff: data, isLoading, error };
}
```

### 7.2 Memoized Computations

```typescript
// hooks/usePayoffChartData.ts
import { useMemo } from 'react';
import type { PayoffCurve } from '@/types/payoff-engine';

export function usePayoffChartData(curve: PayoffCurve | null) {
  return useMemo(() => {
    if (!curve) return null;

    return {
      chartData: curve.points.map((p) => ({
        x: p.spot,
        y: p.pnl,
        isProfitable: p.pnl > 0,
      })),
      
      breakevensData: curve.breakevens.map((be) => ({
        x: be,
        y: 0,
      })),
      
      profitZone: curve.points.filter((p) => p.pnl > 0),
      lossZone: curve.points.filter((p) => p.pnl < 0),
    };
  }, [curve]);
}
```

---

## 8. Common Pitfalls

### 8.1 ❌ Don't Calculate Greeks Client-Side

```typescript
// ❌ WRONG - Don't do this!
function calculateDeltaOnClient(spot, strike, iv, dte) {
  // This is the backend's job
  const d1 = (Math.log(spot / strike) + ...) / ...
  return normalCDF(d1);
}

// ✅ CORRECT - Fetch from backend
const { greeks } = await payoffClient.calculateGreeks(spot, strike, iv, dte, 'CE');
```

### 8.2 ❌ Don't Ignore Quality Flags

```typescript
// ❌ WRONG - Ignoring stale data
function renderPrice(snapshot: DepthSnapshot) {
  return snapshot.trade.last_price;
}

// ✅ CORRECT - Handle stale data
function renderPrice(snapshot: DepthSnapshot) {
  if (snapshot.is_stale) {
    return (
      <span className="text-yellow-500">
        {snapshot.trade.last_price} (stale)
      </span>
    );
  }
  return snapshot.trade.last_price;
}
```

### 8.3 ❌ Don't Confuse Token Types

```typescript
// ❌ WRONG - Using instrument_token as ID
const instrumentId = kiteData.instrument_token; // Wrong!

// ✅ CORRECT - Always use exchange_token as instrument_id
const instrumentId = instrumentInfo.exchange_token;
const symbol = `${instrumentInfo.exchange}:${instrumentId}`;
```

### 8.4 ❌ Don't Hardcode Lot Sizes

```typescript
// ❌ WRONG - Hardcoded lot size
const totalQty = quantity * 25; // Assumes NIFTY

// ✅ CORRECT - Get from instrument info
const totalQty = quantity * instrumentInfo.lot_size;
```

### 8.5 ❌ Don't Render Unvalidated Data

```typescript
// ❌ WRONG - No validation
function OptionChainRow({ call, put }) {
  return (
    <tr>
      <td>{call.iv * 100}%</td>
      <td>{put.iv * 100}%</td>
    </tr>
  );
}

// ✅ CORRECT - Handle missing/invalid data
function OptionChainRow({ call, put }) {
  return (
    <tr>
      <td>{call?.iv != null ? `${(call.iv * 100).toFixed(2)}%` : '-'}</td>
      <td>{put?.iv != null ? `${(put.iv * 100).toFixed(2)}%` : '-'}</td>
    </tr>
  );
}
```

---

## 🔗 Related Documents

- [DATA_SCHEMA.md](./DATA_SCHEMA.md) - Complete type definitions
- [API_REFERENCE.md](./API_REFERENCE.md) - Endpoint documentation
- [WEBSOCKET_PROTOCOL.md](./WEBSOCKET_PROTOCOL.md) - Real-time streaming
