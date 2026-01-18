'use client';

import { useStrategyStore } from '@/lib/store';
import { formatCurrency, formatNumber, cn } from '@/lib/utils';
import { TrendingUp, TrendingDown, MoreHorizontal, Eye, Trash2 } from 'lucide-react';
import { useState } from 'react';

export function ActiveStrategies() {
  const { savedStrategies } = useStrategyStore();
  const [selectedId, setSelectedId] = useState<string | null>(null);

  // Mock real-time P&L updates
  const strategies = savedStrategies.map((strat) => ({
    ...strat,
    currentPnl: Math.random() * 10000 - 5000,
    pnlChange: Math.random() * 200 - 100,
    spot: 26300 + Math.random() * 200 - 100,
  }));

  if (strategies.length === 0) {
    return (
      <div className="p-8 text-center text-foreground-muted">
        <p className="mb-2">No saved strategies</p>
        <p className="text-sm">Build and save a strategy to monitor it here</p>
      </div>
    );
  }

  return (
    <div className="divide-y divide-border">
      {strategies.map((strat) => {
        const isProfit = strat.currentPnl >= 0;
        const isSelected = selectedId === strat.id;

        return (
          <div
            key={strat.id}
            className={cn(
              'p-4 hover:bg-background-tertiary/50 transition-colors cursor-pointer',
              isSelected && 'bg-accent/5'
            )}
            onClick={() => setSelectedId(isSelected ? null : strat.id!)}
          >
            <div className="flex items-center justify-between">
              <div className="flex items-center gap-4">
                {/* P&L Indicator */}
                <div
                  className={cn(
                    'w-10 h-10 rounded-lg flex items-center justify-center',
                    isProfit ? 'bg-profit/10' : 'bg-loss/10'
                  )}
                >
                  {isProfit ? (
                    <TrendingUp className="w-5 h-5 text-profit" />
                  ) : (
                    <TrendingDown className="w-5 h-5 text-loss" />
                  )}
                </div>

                {/* Strategy Info */}
                <div>
                  <div className="flex items-center gap-2">
                    <span className="font-medium">{strat.underlying}</span>
                    <span className="text-xs text-foreground-muted">
                      {strat.legs.length} legs
                    </span>
                  </div>
                  <div className="text-sm text-foreground-muted">
                    {strat.legs.map((l) => `${l.side === 'BUY' ? 'B' : 'S'} ${l.strike}${l.type}`).join(' | ')}
                  </div>
                </div>
              </div>

              {/* P&L Display */}
              <div className="flex items-center gap-6">
                <div className="text-right">
                  <div
                    className={cn(
                      'text-lg font-mono font-semibold',
                      isProfit ? 'text-profit' : 'text-loss'
                    )}
                  >
                    {isProfit ? '+' : ''}{formatCurrency(strat.currentPnl)}
                  </div>
                  <div
                    className={cn(
                      'text-xs font-mono',
                      strat.pnlChange >= 0 ? 'text-profit' : 'text-loss'
                    )}
                  >
                    {strat.pnlChange >= 0 ? '+' : ''}{formatCurrency(strat.pnlChange)} this minute
                  </div>
                </div>

                {/* Spot */}
                <div className="text-right">
                  <div className="text-sm text-foreground-muted">Spot</div>
                  <div className="font-mono">{formatNumber(strat.spot, 2)}</div>
                </div>

                {/* Actions */}
                <div className="flex items-center gap-1">
                  <button className="p-2 rounded-lg hover:bg-background-tertiary text-foreground-muted hover:text-foreground transition-colors">
                    <Eye className="w-4 h-4" />
                  </button>
                  <button className="p-2 rounded-lg hover:bg-background-tertiary text-foreground-muted hover:text-foreground transition-colors">
                    <MoreHorizontal className="w-4 h-4" />
                  </button>
                </div>
              </div>
            </div>

            {/* Expanded Details */}
            {isSelected && (
              <div className="mt-4 pt-4 border-t border-border grid grid-cols-4 gap-4 text-sm">
                <div>
                  <span className="text-foreground-muted block mb-1">Max Profit</span>
                  <span className="font-mono text-profit">₹15,000</span>
                </div>
                <div>
                  <span className="text-foreground-muted block mb-1">Max Loss</span>
                  <span className="font-mono text-loss">₹8,500</span>
                </div>
                <div>
                  <span className="text-foreground-muted block mb-1">Breakeven</span>
                  <span className="font-mono">26,250 / 26,450</span>
                </div>
                <div>
                  <span className="text-foreground-muted block mb-1">Days to Expiry</span>
                  <span className="font-mono">5 days</span>
                </div>
              </div>
            )}
          </div>
        );
      })}
    </div>
  );
}
