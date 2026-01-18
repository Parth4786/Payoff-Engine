'use client';

import { GreeksDisplay, PnLDisplay } from '@/components/shared';
import { formatNumber, formatCurrency, cn } from '@/lib/utils';
import { TrendingUp, TrendingDown, Activity, Percent, Clock } from 'lucide-react';
import type { ReplaySnapshot } from '@/lib/types';

interface Props {
  snapshot: ReplaySnapshot | null;
}

export function MarketStatePanel({ snapshot }: Props) {
  if (!snapshot) {
    return (
      <div className="h-48 flex items-center justify-center text-foreground-muted">
        No snapshot selected
      </div>
    );
  }

  const isProfit = snapshot.total_pnl >= 0;
  const time = new Date(snapshot.timestamp).toLocaleTimeString('en-IN', {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });

  return (
    <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
      {/* Underlying Price */}
      <div className="p-4 rounded-lg bg-background-tertiary/50">
        <div className="flex items-center justify-between mb-2">
          <span className="text-xs text-foreground-muted">Underlying Price</span>
          {isProfit ? (
            <TrendingUp className="w-4 h-4 text-profit" />
          ) : (
            <TrendingDown className="w-4 h-4 text-loss" />
          )}
        </div>
        <div className="text-2xl font-mono font-semibold">
          {formatNumber(snapshot.underlying_price, 2)}
        </div>
      </div>

      {/* Time */}
      <div className="p-4 rounded-lg bg-background-tertiary/50">
        <div className="flex items-center justify-between mb-2">
          <span className="text-xs text-foreground-muted">Snapshot Time</span>
          <Clock className="w-4 h-4 text-accent" />
        </div>
        <div className="text-2xl font-mono font-semibold">
          {time}
        </div>
        <div className="text-xs text-foreground-muted mt-1">
          {new Date(snapshot.timestamp).toLocaleDateString('en-IN')}
        </div>
      </div>

      {/* P&L */}
      <div className="p-4 rounded-lg bg-background-tertiary/50">
        <div className="flex items-center justify-between mb-2">
          <span className="text-xs text-foreground-muted">Total P&L</span>
          <Activity className="w-4 h-4 text-info" />
        </div>
        <PnLDisplay value={snapshot.total_pnl || 0} size="lg" />
      </div>

      {/* Greeks Snapshot */}
      <div className="p-4 rounded-lg bg-background-tertiary/50">
        <div className="flex items-center justify-between mb-2">
          <span className="text-xs text-foreground-muted">Greeks</span>
        </div>
        {snapshot.greeks ? (
          <div className="grid grid-cols-2 gap-2 text-xs">
            <div>
              <span className="text-foreground-muted">Δ:</span>
              <span className="ml-1 font-mono">{snapshot.greeks.delta.toFixed(3)}</span>
            </div>
            <div>
              <span className="text-foreground-muted">Γ:</span>
              <span className="ml-1 font-mono">{snapshot.greeks.gamma.toFixed(4)}</span>
            </div>
            <div>
              <span className="text-foreground-muted">Θ:</span>
              <span className="ml-1 font-mono">{snapshot.greeks.theta.toFixed(2)}</span>
            </div>
            <div>
              <span className="text-foreground-muted">ν:</span>
              <span className="ml-1 font-mono">{snapshot.greeks.vega.toFixed(2)}</span>
            </div>
          </div>
        ) : (
          <div className="text-sm text-foreground-muted">No data</div>
        )}
      </div>
    </div>
  );
}
