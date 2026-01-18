'use client';

import { cn, formatCurrency, formatPercent, getPnLColor, getPnLBgColor } from '@/lib/utils';

interface PnLDisplayProps {
  value: number;
  percentage?: number;
  label?: string;
  size?: 'sm' | 'md' | 'lg' | 'xl';
  showSign?: boolean;
  compact?: boolean;
  className?: string;
}

export function PnLDisplay({
  value,
  percentage,
  label,
  size = 'md',
  showSign = true,
  compact = false,
  className,
}: PnLDisplayProps) {
  const sizeClasses = {
    sm: 'text-sm',
    md: 'text-base',
    lg: 'text-xl',
    xl: 'text-3xl',
  };

  const formattedValue = formatCurrency(Math.abs(value), {
    decimals: size === 'xl' ? 0 : 2,
  });
  const sign = value >= 0 ? '+' : '-';
  const displayValue = showSign ? `${sign}${formattedValue}` : formattedValue;

  if (compact) {
    return (
      <span className={cn('font-mono font-medium', sizeClasses[size], getPnLColor(value), className)}>
        {displayValue}
        {percentage !== undefined && (
          <span className="ml-1 text-xs opacity-70">({formatPercent(percentage)})</span>
        )}
      </span>
    );
  }

  return (
    <div className={cn('flex flex-col', className)}>
      {label && <span className="text-xs text-foreground-muted mb-0.5">{label}</span>}
      <div className={cn('flex items-baseline gap-2', sizeClasses[size])}>
        <span className={cn('font-mono font-semibold', getPnLColor(value))}>
          {displayValue}
        </span>
        {percentage !== undefined && (
          <span
            className={cn(
              'stat-badge',
              value >= 0 ? 'stat-badge-profit' : 'stat-badge-loss'
            )}
          >
            {formatPercent(percentage)}
          </span>
        )}
      </div>
    </div>
  );
}

interface PnLBadgeProps {
  value: number;
  size?: 'sm' | 'md';
  className?: string;
}

export function PnLBadge({ value, size = 'md', className }: PnLBadgeProps) {
  const sign = value >= 0 ? '+' : '';
  
  return (
    <span
      className={cn(
        'inline-flex items-center font-mono font-medium rounded',
        size === 'sm' ? 'px-1.5 py-0.5 text-xs' : 'px-2 py-1 text-sm',
        getPnLBgColor(value),
        getPnLColor(value),
        className
      )}
    >
      {sign}{formatCurrency(value, { decimals: 0 })}
    </span>
  );
}

interface PnLBarProps {
  value: number;
  max: number;
  min: number;
  height?: number;
  className?: string;
}

export function PnLBar({ value, max, min, height = 4, className }: PnLBarProps) {
  const range = max - min;
  const zeroPosition = ((0 - min) / range) * 100;
  const valuePosition = ((value - min) / range) * 100;
  
  const isPositive = value >= 0;
  const barStart = isPositive ? zeroPosition : valuePosition;
  const barWidth = Math.abs(valuePosition - zeroPosition);

  return (
    <div className={cn('relative w-full bg-background-tertiary rounded', className)} style={{ height }}>
      {/* Zero line */}
      <div
        className="absolute top-0 bottom-0 w-px bg-border"
        style={{ left: `${zeroPosition}%` }}
      />
      {/* Value bar */}
      <div
        className={cn(
          'absolute top-0 bottom-0 rounded',
          isPositive ? 'bg-profit' : 'bg-loss'
        )}
        style={{
          left: `${barStart}%`,
          width: `${barWidth}%`,
        }}
      />
    </div>
  );
}
