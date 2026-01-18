'use client';

import { useMemo } from 'react';
import { useStrategy } from '@/hooks';
import { cn, formatCurrency } from '@/lib/utils';

interface Props {
  scenario: 'at-expiry' | 'now' | 'custom';
  daysToExpiry: number;
}

export function PayoffTable({ scenario, daysToExpiry }: Props) {
  const { payoffResult, strategy } = useStrategy();

  // Generate spot levels
  const spotLevels = useMemo(() => {
    const base = 26300; // TODO: use actual spot
    const step = 100;
    const levels: number[] = [];
    
    for (let i = -10; i <= 10; i++) {
      levels.push(base + i * step);
    }
    return levels;
  }, []);

  // Get P&L for each level from payoff curve
  const tableData = useMemo(() => {
    if (!payoffResult?.points || payoffResult.points.length === 0) {
      return spotLevels.map((spot) => ({
        spot,
        pnl: 0,
        pnlPct: 0,
      }));
    }

    return spotLevels.map((spot) => {
      // Find closest point in payoff curve
      const point = payoffResult.points.reduce((closest, p) => {
        return Math.abs(p.spot - spot) < Math.abs(closest.spot - spot) ? p : closest;
      });

      const pnl = point.pnl;
      const investment = Math.abs(payoffResult.net_premium) || 1;
      const pnlPct = (pnl / investment) * 100;

      return {
        spot,
        pnl,
        pnlPct,
      };
    });
  }, [payoffResult, spotLevels, scenario]);

  const currentSpot = 26300; // TODO: use actual

  return (
    <div className="max-h-[400px] overflow-auto">
      <table className="w-full text-sm">
        <thead className="sticky top-0 bg-background-secondary z-10">
          <tr className="border-b border-border">
            <th className="px-4 py-3 text-left text-foreground-muted font-medium">
              Spot Level
            </th>
            <th className="px-4 py-3 text-right text-foreground-muted font-medium">
              P&L (₹)
            </th>
            <th className="px-4 py-3 text-right text-foreground-muted font-medium">
              P&L (%)
            </th>
            <th className="px-4 py-3 text-right text-foreground-muted font-medium">
              Visual
            </th>
          </tr>
        </thead>
        <tbody>
          {tableData.map(({ spot, pnl, pnlPct }) => {
            const isProfit = pnl >= 0;
            const isCurrentSpot = spot === currentSpot;
            const isBreakeven = payoffResult?.breakevens?.some(
              (be) => Math.abs(be - spot) < 50
            );

            return (
              <tr
                key={spot}
                className={cn(
                  'border-b border-border/50 hover:bg-background-tertiary/50 transition-colors',
                  isCurrentSpot && 'bg-accent/5',
                  isBreakeven && 'bg-warning/5'
                )}
              >
                <td className="px-4 py-2 font-mono">
                  {formatCurrency(spot, { decimals: 0, symbol: '' })}
                  {isCurrentSpot && (
                    <span className="ml-2 text-xs text-accent">(Current)</span>
                  )}
                  {isBreakeven && (
                    <span className="ml-2 text-xs text-warning">(BE)</span>
                  )}
                </td>
                <td
                  className={cn(
                    'px-4 py-2 font-mono text-right',
                    isProfit ? 'text-profit' : 'text-loss'
                  )}
                >
                  {formatCurrency(pnl)}
                </td>
                <td
                  className={cn(
                    'px-4 py-2 font-mono text-right',
                    isProfit ? 'text-profit' : 'text-loss'
                  )}
                >
                  {pnlPct >= 0 ? '+' : ''}
                  {pnlPct.toFixed(1)}%
                </td>
                <td className="px-4 py-2">
                  <div className="flex items-center justify-end">
                    <div className="w-24 h-2 bg-background-tertiary rounded-full overflow-hidden">
                      <div
                        className={cn(
                          'h-full transition-all',
                          isProfit ? 'bg-profit' : 'bg-loss'
                        )}
                        style={{
                          width: `${Math.min(Math.abs(pnlPct), 100)}%`,
                          marginLeft: isProfit ? '50%' : `${50 - Math.min(Math.abs(pnlPct) / 2, 50)}%`,
                        }}
                      />
                    </div>
                  </div>
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}
