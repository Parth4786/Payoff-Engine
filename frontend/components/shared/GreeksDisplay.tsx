import { cn, formatCurrency, formatGreek, getPnLColor } from '@/lib/utils';
import type { Greeks } from '@/lib/types';

interface GreeksDisplayProps {
  greeks: Greeks;
  compact?: boolean;
  showRho?: boolean;
  className?: string;
}

export function GreeksDisplay({ greeks, compact = false, showRho = false, className }: GreeksDisplayProps) {
  const items = [
    { label: 'Δ Delta', value: greeks.delta, type: 'delta' as const },
    { label: 'Γ Gamma', value: greeks.gamma, type: 'gamma' as const },
    { label: 'Θ Theta', value: greeks.theta, type: 'theta' as const, isCurrency: true },
    { label: 'V Vega', value: greeks.vega, type: 'vega' as const, isCurrency: true },
    ...(showRho ? [{ label: 'ρ Rho', value: greeks.rho, type: 'rho' as const, isCurrency: true }] : []),
  ];

  if (compact) {
    return (
      <div className={cn('flex items-center gap-4 text-sm', className)}>
        {items.map((item) => (
          <div key={item.type} className="flex items-center gap-1.5">
            <span className="text-foreground-muted">{item.label.split(' ')[0]}</span>
            <span className={cn('font-mono', getPnLColor(item.value))}>
              {item.isCurrency
                ? formatCurrency(item.value, { decimals: 0 })
                : formatGreek(item.value, item.type)}
            </span>
          </div>
        ))}
      </div>
    );
  }

  return (
    <div className={cn('grid grid-cols-4 gap-3', showRho && 'grid-cols-5', className)}>
      {items.map((item) => (
        <div key={item.type} className="greek-badge">
          <span className="greek-label">{item.label}</span>
          <span className={cn('greek-value', getPnLColor(item.value))}>
            {item.isCurrency
              ? formatCurrency(item.value, { decimals: 0 })
              : formatGreek(item.value, item.type)}
          </span>
        </div>
      ))}
    </div>
  );
}

interface SingleGreekProps {
  label: string;
  value: number;
  type: 'delta' | 'gamma' | 'theta' | 'vega' | 'rho';
  isCurrency?: boolean;
  size?: 'sm' | 'md' | 'lg';
}

export function SingleGreek({ label, value, type, isCurrency, size = 'md' }: SingleGreekProps) {
  const sizeClasses = {
    sm: 'text-xs',
    md: 'text-sm',
    lg: 'text-base',
  };

  return (
    <div className="flex flex-col items-center gap-0.5">
      <span className={cn('text-foreground-muted', sizeClasses[size])}>{label}</span>
      <span className={cn('font-mono font-medium', sizeClasses[size], getPnLColor(value))}>
        {isCurrency
          ? formatCurrency(value, { decimals: 0 })
          : formatGreek(value, type)}
      </span>
    </div>
  );
}
