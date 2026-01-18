'use client';

import { useStrategy } from '@/hooks';
import { formatCurrency } from '@/lib/utils';
import { PnLDisplay } from '@/components/shared';
import { ArrowUpRight, ArrowDownRight, Target, TrendingUp, Percent, Clock } from 'lucide-react';

export function KeyMetrics() {
  const { payoffResult, strategy, netPremium, aggregateGreeks } = useStrategy();

  const metrics = [
    {
      label: 'Max Profit',
      value: payoffResult?.max_profit,
      format: (v: number) => v === Infinity ? 'Unlimited' : formatCurrency(v),
      icon: ArrowUpRight,
      color: 'text-profit',
      bgColor: 'bg-profit/10',
    },
    {
      label: 'Max Loss',
      value: payoffResult?.max_loss,
      format: (v: number) => v === -Infinity ? 'Unlimited' : formatCurrency(Math.abs(v)),
      icon: ArrowDownRight,
      color: 'text-loss',
      bgColor: 'bg-loss/10',
    },
    {
      label: 'Net Premium',
      value: netPremium,
      format: (v: number) => formatCurrency(v),
      icon: Target,
      color: netPremium >= 0 ? 'text-profit' : 'text-loss',
      bgColor: netPremium >= 0 ? 'bg-profit/10' : 'bg-loss/10',
    },
    {
      label: 'Risk/Reward',
      value: payoffResult?.max_loss && payoffResult?.max_profit
        ? Math.abs(payoffResult.max_profit / payoffResult.max_loss)
        : null,
      format: (v: number) => v === Infinity || v === -Infinity ? '-' : `1:${v.toFixed(1)}`,
      icon: TrendingUp,
      color: 'text-accent',
      bgColor: 'bg-accent/10',
    },
    {
      label: 'Breakevens',
      value: payoffResult?.breakevens?.length || 0,
      format: (v: number) => v.toString(),
      icon: Target,
      color: 'text-warning',
      bgColor: 'bg-warning/10',
    },
    {
      label: 'Theta/Day',
      value: aggregateGreeks?.theta,
      format: (v: number) => formatCurrency(v, { decimals: 2 }),
      icon: Clock,
      color: (aggregateGreeks?.theta || 0) >= 0 ? 'text-profit' : 'text-loss',
      bgColor: (aggregateGreeks?.theta || 0) >= 0 ? 'bg-profit/10' : 'bg-loss/10',
    },
  ];

  return (
    <div className="space-y-3">
      {metrics.map((metric) => {
        const Icon = metric.icon;
        const hasValue = metric.value !== null && metric.value !== undefined;
        const colorClass = typeof metric.color === 'function' ? metric.color : metric.color;
        const bgColorClass = typeof metric.bgColor === 'function' ? metric.bgColor : metric.bgColor;

        return (
          <div
            key={metric.label}
            className="flex items-center justify-between p-3 rounded-lg bg-background-tertiary/50 hover:bg-background-tertiary transition-colors"
          >
            <div className="flex items-center gap-3">
              <div className={`p-2 rounded-lg ${bgColorClass}`}>
                <Icon className={`w-4 h-4 ${colorClass}`} />
              </div>
              <span className="text-sm text-foreground-muted">{metric.label}</span>
            </div>
            <span className={`text-sm font-mono font-semibold ${hasValue ? colorClass : 'text-foreground-muted'}`}>
              {hasValue ? metric.format(metric.value!) : '-'}
            </span>
          </div>
        );
      })}

      {/* Breakeven Values */}
      {payoffResult?.breakevens && payoffResult.breakevens.length > 0 && (
        <div className="pt-3 border-t border-border">
          <span className="text-xs text-foreground-muted block mb-2">Breakeven Levels</span>
          <div className="flex flex-wrap gap-2">
            {payoffResult.breakevens.map((be, i) => (
              <span
                key={i}
                className="px-3 py-1.5 rounded-lg bg-warning/10 text-warning font-mono text-sm"
              >
                {formatCurrency(be, { decimals: 0, symbol: '' })}
              </span>
            ))}
          </div>
        </div>
      )}

      {/* Strategy Info */}
      {payoffResult?.strategy && (
        <div className="pt-3 border-t border-border">
          <span className="text-xs text-foreground-muted block mb-2">Detected Strategy</span>
          <span className="px-3 py-1.5 rounded-lg bg-accent/10 text-accent font-medium text-sm inline-block">
            {payoffResult.strategy}
          </span>
        </div>
      )}
    </div>
  );
}
