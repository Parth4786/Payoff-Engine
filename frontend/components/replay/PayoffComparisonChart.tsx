'use client';

import { useMemo } from 'react';
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ReferenceLine,
  ResponsiveContainer,
  Area,
  ComposedChart,
} from 'recharts';
import { formatCurrency, formatNumber } from '@/lib/utils';
import type { ReplaySnapshot, CompareResponse } from '@/lib/types';

interface Props {
  snapshots: ReplaySnapshot[];
  currentIndex: number;
  comparison: CompareResponse | null;
}

export function PayoffComparisonChart({ snapshots, currentIndex, comparison }: Props) {
  // Get predicted P&L from comparison if available
  const predictedPnl = comparison?.at_prediction_time.predicted_pnl_at_current_spot;

  // Prepare chart data
  const chartData = useMemo(() => {
    return snapshots.map((snap, index) => {
      return {
        index,
        timestamp: snap.timestamp,
        time: new Date(snap.timestamp).toLocaleTimeString('en-IN', {
          hour: '2-digit',
          minute: '2-digit',
        }),
        pnl: snap.total_pnl || 0,
        predictedPnl: comparison ? predictedPnl : undefined,
        spot: snap.underlying_price,
      };
    });
  }, [snapshots, comparison, predictedPnl]);

  const formatTime = (time: string) => time;

  // Custom tooltip
  const CustomTooltip = ({ active, payload, label }: any) => {
    if (!active || !payload?.length) return null;

    const data = payload[0]?.payload;

    return (
      <div className="bg-background-secondary border border-border rounded-lg p-3 shadow-lg">
        <p className="text-xs text-foreground-muted mb-2">{data.time}</p>
        <div className="space-y-1">
          <p className="text-sm">
            <span className="text-foreground-muted">Actual:</span>
            <span className={`ml-2 font-mono font-semibold ${data.pnl >= 0 ? 'text-profit' : 'text-loss'}`}>
              {formatCurrency(data.pnl)}
            </span>
          </p>
          {data.predictedPnl !== undefined && (
            <p className="text-sm">
              <span className="text-foreground-muted">Predicted:</span>
              <span className="ml-2 font-mono text-info">
                {formatCurrency(data.predictedPnl)}
              </span>
            </p>
          )}
          <p className="text-sm">
            <span className="text-foreground-muted">Spot:</span>
            <span className="ml-2 font-mono">{formatNumber(data.spot, 2)}</span>
          </p>
        </div>
      </div>
    );
  };

  if (snapshots.length === 0) {
    return (
      <div className="h-[300px] flex items-center justify-center text-foreground-muted">
        No data to display
      </div>
    );
  }

  return (
    <div className="h-[300px] w-full">
      <ResponsiveContainer width="100%" height="100%">
        <ComposedChart
          data={chartData}
          margin={{ top: 20, right: 30, left: 20, bottom: 20 }}
        >
          <defs>
            <linearGradient id="pnlGradient" x1="0" y1="0" x2="0" y2="1">
              <stop offset="5%" stopColor="hsl(var(--accent))" stopOpacity={0.2} />
              <stop offset="95%" stopColor="hsl(var(--accent))" stopOpacity={0} />
            </linearGradient>
          </defs>

          <CartesianGrid
            strokeDasharray="3 3"
            stroke="hsl(var(--border))"
            opacity={0.5}
          />

          <XAxis
            dataKey="time"
            tick={{ fill: 'hsl(var(--foreground-muted))', fontSize: 10 }}
            axisLine={{ stroke: 'hsl(var(--border))' }}
            tickLine={{ stroke: 'hsl(var(--border))' }}
            interval="preserveStartEnd"
          />

          <YAxis
            tickFormatter={(v) => formatCurrency(v, { decimals: 0 })}
            tick={{ fill: 'hsl(var(--foreground-muted))', fontSize: 10 }}
            axisLine={{ stroke: 'hsl(var(--border))' }}
            tickLine={{ stroke: 'hsl(var(--border))' }}
          />

          <Tooltip content={<CustomTooltip />} />

          {/* Zero line */}
          <ReferenceLine
            y={0}
            stroke="hsl(var(--foreground-muted))"
            strokeDasharray="4 4"
            strokeWidth={1}
          />

          {/* Current position marker */}
          <ReferenceLine
            x={currentIndex}
            stroke="hsl(var(--accent))"
            strokeWidth={2}
            strokeDasharray="4 4"
          />

          {/* Area under P&L curve */}
          <Area
            type="monotone"
            dataKey="pnl"
            stroke="transparent"
            fill="url(#pnlGradient)"
          />

          {/* Actual P&L Line */}
          <Line
            type="monotone"
            dataKey="pnl"
            stroke="hsl(var(--foreground))"
            strokeWidth={2}
            dot={false}
            name="Actual P&L"
          />

          {/* Predicted P&L Line (if available) */}
          {comparison && (
            <Line
              type="monotone"
              dataKey="predictedPnl"
              stroke="hsl(var(--info))"
              strokeWidth={2}
              strokeDasharray="4 4"
              dot={false}
              name="Predicted P&L"
            />
          )}
        </ComposedChart>
      </ResponsiveContainer>
    </div>
  );
}
