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
import { useStrategy } from '@/hooks';
import { formatCurrency } from '@/lib/utils';

interface Props {
  scenario: 'at-expiry' | 'now' | 'custom';
  daysToExpiry?: number;
}

export function PayoffChart({ scenario, daysToExpiry }: Props) {
  const { payoffResult, strategy } = useStrategy();

  const fallbackSpot = payoffResult?.spot ?? 26300;

  // Generate payoff curve data
  const chartData = useMemo(() => {
    if (!payoffResult?.points || payoffResult.points.length === 0) {
      // Generate placeholder data
      const spotBase = fallbackSpot;
      const range = 2000;
      const points: any[] = [];
      
      for (let i = -range; i <= range; i += 50) {
        points.push({
          spot: spotBase + i,
          pnl: 0,
        });
      }
      return points;
    }

    return payoffResult.points.map((point) => ({
      spot: point.spot,
      pnl: point.pnl,
      pnlNow: point.pnl,
    }));
  }, [payoffResult?.points]);

  // Find key points
  const breakevens = payoffResult?.breakevens || [];
  const currentSpot = fallbackSpot;

  // Custom tooltip
  const CustomTooltip = ({ active, payload, label }: any) => {
    if (!active || !payload?.length) return null;

    const pnl = payload[0]?.value || 0;
    const isProfit = pnl >= 0;

    return (
      <div className="bg-background-secondary border border-border rounded-lg p-3 shadow-lg">
        <p className="text-xs text-foreground-muted mb-1">Spot: {formatCurrency(label, { decimals: 0, symbol: '' })}</p>
        <p className={`text-sm font-semibold ${isProfit ? 'text-profit' : 'text-loss'}`}>
          P&L: {formatCurrency(pnl)}
        </p>
        {payload[1] && (
          <p className="text-xs text-foreground-muted mt-1">
            Now: {formatCurrency(payload[1].value)}
          </p>
        )}
      </div>
    );
  };

  return (
    <div className="h-[400px] w-full">
      <ResponsiveContainer width="100%" height="100%">
        <ComposedChart
          data={chartData}
          margin={{ top: 20, right: 30, left: 20, bottom: 20 }}
        >
          <defs>
            {/* Gradient for profit zone */}
            <linearGradient id="profitGradient" x1="0" y1="0" x2="0" y2="1">
              <stop offset="5%" stopColor="hsl(var(--profit))" stopOpacity={0.3} />
              <stop offset="95%" stopColor="hsl(var(--profit))" stopOpacity={0} />
            </linearGradient>
            {/* Gradient for loss zone */}
            <linearGradient id="lossGradient" x1="0" y1="1" x2="0" y2="0">
              <stop offset="5%" stopColor="hsl(var(--loss))" stopOpacity={0.3} />
              <stop offset="95%" stopColor="hsl(var(--loss))" stopOpacity={0} />
            </linearGradient>
          </defs>

          <CartesianGrid
            strokeDasharray="3 3"
            stroke="hsl(var(--border))"
            opacity={0.5}
          />

          <XAxis
            dataKey="spot"
            type="number"
            domain={['dataMin', 'dataMax']}
            tickFormatter={(v) => formatCurrency(v, { decimals: 0, symbol: '' })}
            tick={{ fill: 'hsl(var(--foreground-muted))', fontSize: 11 }}
            axisLine={{ stroke: 'hsl(var(--border))' }}
            tickLine={{ stroke: 'hsl(var(--border))' }}
          />

          <YAxis
            tickFormatter={(v) => formatCurrency(v, { decimals: 0 })}
            tick={{ fill: 'hsl(var(--foreground-muted))', fontSize: 11 }}
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

          {/* Breakeven lines */}
          {breakevens.map((be, i) => (
            <ReferenceLine
              key={`be-${i}`}
              x={be}
              stroke="hsl(var(--warning))"
              strokeDasharray="4 4"
              strokeWidth={1}
              label={{
                value: `BE ${formatCurrency(be, { decimals: 0, symbol: '' })}`,
                fill: 'hsl(var(--warning))',
                fontSize: 10,
                position: 'top',
              }}
            />
          ))}

          {/* Current spot line */}
          <ReferenceLine
            x={currentSpot}
            stroke="hsl(var(--accent))"
            strokeWidth={2}
            label={{
              value: 'Spot',
              fill: 'hsl(var(--accent))',
              fontSize: 10,
              position: 'top',
            }}
          />

          {/* P&L Now curve (if showing) */}
          {scenario === 'now' && (
            <Line
              type="monotone"
              dataKey="pnlNow"
              stroke="hsl(var(--accent))"
              strokeWidth={2}
              dot={false}
              name="P&L Now"
            />
          )}

          {/* Main P&L at expiry curve */}
          <Line
            type="monotone"
            dataKey="pnl"
            stroke="hsl(var(--foreground))"
            strokeWidth={2}
            dot={false}
            name="P&L at Expiry"
          />
        </ComposedChart>
      </ResponsiveContainer>
    </div>
  );
}
