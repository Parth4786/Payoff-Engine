'use client';

import { useMemo } from 'react';
import { cn, formatCurrency, formatNumber } from '@/lib/utils';
import type { ReplaySnapshot, CompareResponse } from '@/lib/types';

interface Props {
  snapshots: ReplaySnapshot[];
  comparison: CompareResponse | null;
}

export function DeviationTable({ snapshots, comparison }: Props) {
  // Build summary and snapshot-based deviations
  const summaryData = useMemo(() => {
    if (!comparison) return null;

    const pnlDeviation = comparison.at_actual_time.actual_pnl - 
      comparison.at_prediction_time.predicted_pnl_at_current_spot;
    const priceDeviation = comparison.at_actual_time.underlying_price - 
      comparison.at_prediction_time.underlying_price;

    return {
      predictionTime: new Date(comparison.prediction_timestamp).toLocaleString('en-IN'),
      actualTime: new Date(comparison.actual_timestamp).toLocaleString('en-IN'),
      elapsed: comparison.time_elapsed_hours,
      predicted: {
        price: comparison.at_prediction_time.underlying_price,
        pnl: comparison.at_prediction_time.predicted_pnl_at_current_spot,
        breakeven: comparison.at_prediction_time.predicted_breakeven,
      },
      actual: {
        price: comparison.at_actual_time.underlying_price,
        pnl: comparison.at_actual_time.actual_pnl,
        breakeven: comparison.at_actual_time.actual_breakeven,
      },
      deviation: {
        pnl: pnlDeviation,
        price: priceDeviation,
        accuracy: comparison.deviation.prediction_accuracy_score,
      },
    };
  }, [comparison]);

  // Show recent snapshots with their P&L
  const snapshotData = useMemo(() => {
    return snapshots.slice(-10).map((snap) => ({
      timestamp: snap.timestamp,
      time: new Date(snap.timestamp).toLocaleTimeString('en-IN', {
        hour: '2-digit',
        minute: '2-digit',
      }),
      spot: snap.underlying_price,
      pnl: snap.total_pnl || 0,
    }));
  }, [snapshots]);

  if (!comparison) {
    return (
      <div className="p-6 text-center text-foreground-muted text-sm">
        Load comparison to see deviations
      </div>
    );
  }

  return (
    <div className="space-y-4">
      {/* Summary Comparison */}
      {summaryData && (
        <div className="grid grid-cols-3 gap-4 p-4 bg-background-tertiary/50 rounded-lg">
          <div>
            <div className="text-xs text-foreground-muted mb-1">Predicted P&L</div>
            <div className={cn(
              'font-mono font-medium',
              summaryData.predicted.pnl >= 0 ? 'text-profit' : 'text-loss'
            )}>
              {formatCurrency(summaryData.predicted.pnl)}
            </div>
          </div>
          <div>
            <div className="text-xs text-foreground-muted mb-1">Actual P&L</div>
            <div className={cn(
              'font-mono font-medium',
              summaryData.actual.pnl >= 0 ? 'text-profit' : 'text-loss'
            )}>
              {formatCurrency(summaryData.actual.pnl)}
            </div>
          </div>
          <div>
            <div className="text-xs text-foreground-muted mb-1">Deviation</div>
            <div className={cn(
              'font-mono font-medium',
              summaryData.deviation.pnl >= 0 ? 'text-profit' : 'text-loss'
            )}>
              {summaryData.deviation.pnl >= 0 ? '+' : ''}
              {formatCurrency(summaryData.deviation.pnl)}
              <span className="text-xs ml-1">({summaryData.deviation.accuracy.toFixed(1)}%)</span>
            </div>
          </div>
        </div>
      )}

      {/* Recent Snapshots */}
      <div className="max-h-[200px] overflow-auto">
        <table className="w-full text-sm">
          <thead className="sticky top-0 bg-background-secondary z-10">
            <tr className="border-b border-border">
              <th className="px-4 py-2 text-left text-foreground-muted font-medium">Time</th>
              <th className="px-4 py-2 text-right text-foreground-muted font-medium">Spot</th>
              <th className="px-4 py-2 text-right text-foreground-muted font-medium">P&L</th>
            </tr>
          </thead>
          <tbody>
            {snapshotData.map((row) => (
              <tr
                key={row.timestamp}
                className="border-b border-border/50 hover:bg-background-tertiary/50 transition-colors"
              >
                <td className="px-4 py-2 text-foreground-secondary">{row.time}</td>
                <td className="px-4 py-2 text-right font-mono">{formatNumber(row.spot, 0)}</td>
                <td
                  className={cn(
                    'px-4 py-2 text-right font-mono',
                    row.pnl >= 0 ? 'text-profit' : 'text-loss'
                  )}
                >
                  {formatCurrency(row.pnl)}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
