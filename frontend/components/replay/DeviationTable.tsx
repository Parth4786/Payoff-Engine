'use client';

import { useMemo } from 'react';
import { cn, formatCurrency, formatNumber } from '@/lib/utils';
import type { ReplaySnapshot, CompareResponse } from '@/lib/types';

interface Props {
  snapshots: ReplaySnapshot[];
  comparison: CompareResponse | null;
}

export function DeviationTable({ snapshots, comparison }: Props) {
  // Calculate deviations between actual and predicted
  const deviations = useMemo(() => {
    if (!comparison?.points) return [];

    return snapshots.slice(0, 20).map((snap) => {
      const predicted = comparison.points.find(
        (p) => Math.abs(p.timestamp - snap.timestamp) < 60000
      );

      const deviation = predicted
        ? ((snap.pnl || 0) - predicted.predicted_pnl)
        : null;

      const deviationPct = predicted && predicted.predicted_pnl !== 0
        ? (deviation! / Math.abs(predicted.predicted_pnl)) * 100
        : null;

      return {
        timestamp: snap.timestamp,
        time: new Date(snap.timestamp).toLocaleTimeString('en-IN', {
          hour: '2-digit',
          minute: '2-digit',
        }),
        actual: snap.pnl || 0,
        predicted: predicted?.predicted_pnl,
        deviation,
        deviationPct,
        spot: snap.spot,
      };
    });
  }, [snapshots, comparison]);

  if (!comparison) {
    return (
      <div className="p-6 text-center text-foreground-muted text-sm">
        Load comparison to see deviations
      </div>
    );
  }

  return (
    <div className="max-h-[300px] overflow-auto">
      <table className="w-full text-sm">
        <thead className="sticky top-0 bg-background-secondary z-10">
          <tr className="border-b border-border">
            <th className="px-4 py-2 text-left text-foreground-muted font-medium">Time</th>
            <th className="px-4 py-2 text-right text-foreground-muted font-medium">Spot</th>
            <th className="px-4 py-2 text-right text-foreground-muted font-medium">Actual</th>
            <th className="px-4 py-2 text-right text-foreground-muted font-medium">Predicted</th>
            <th className="px-4 py-2 text-right text-foreground-muted font-medium">Deviation</th>
          </tr>
        </thead>
        <tbody>
          {deviations.map((row, i) => (
            <tr
              key={row.timestamp}
              className="border-b border-border/50 hover:bg-background-tertiary/50 transition-colors"
            >
              <td className="px-4 py-2 text-foreground-secondary">{row.time}</td>
              <td className="px-4 py-2 text-right font-mono">{formatNumber(row.spot, 0)}</td>
              <td
                className={cn(
                  'px-4 py-2 text-right font-mono',
                  row.actual >= 0 ? 'text-profit' : 'text-loss'
                )}
              >
                {formatCurrency(row.actual)}
              </td>
              <td className="px-4 py-2 text-right font-mono text-info">
                {row.predicted !== undefined ? formatCurrency(row.predicted) : '-'}
              </td>
              <td
                className={cn(
                  'px-4 py-2 text-right font-mono',
                  row.deviation !== null && row.deviation >= 0 ? 'text-profit' : 'text-loss'
                )}
              >
                {row.deviation !== null ? (
                  <>
                    {row.deviation >= 0 ? '+' : ''}
                    {formatCurrency(row.deviation)}
                    <span className="text-xs text-foreground-muted ml-1">
                      ({row.deviationPct?.toFixed(1)}%)
                    </span>
                  </>
                ) : (
                  '-'
                )}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
