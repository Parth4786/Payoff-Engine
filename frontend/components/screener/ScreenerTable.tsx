'use client';

import { useMemo, useRef } from 'react';
import { useVirtualizer } from '@tanstack/react-virtual';
import { cn, formatNumber, formatCurrency } from '@/lib/utils';
import { ArrowUpDown, ChevronRight } from 'lucide-react';
import type { InstrumentSnapshot } from '@/lib/types';

interface Props {
  instruments: InstrumentSnapshot[];
  isLoading: boolean;
  onSelectInstrument: (instrument: InstrumentSnapshot) => void;
}

export function ScreenerTable({ instruments, isLoading, onSelectInstrument }: Props) {
  const parentRef = useRef<HTMLDivElement>(null);

  const rowVirtualizer = useVirtualizer({
    count: instruments.length,
    getScrollElement: () => parentRef.current,
    estimateSize: () => 44,
    overscan: 10,
  });

  const columns = [
    { key: 'symbol', label: 'Symbol', width: 180 },
    { key: 'strike', label: 'Strike', width: 100, align: 'right' as const },
    { key: 'type', label: 'Type', width: 60, align: 'center' as const },
    { key: 'ltp', label: 'LTP', width: 100, align: 'right' as const },
    { key: 'oi_change', label: 'OI Chg', width: 100, align: 'right' as const },
    { key: 'iv', label: 'IV', width: 80, align: 'right' as const },
    { key: 'oi', label: 'OI', width: 100, align: 'right' as const },
    { key: 'volume', label: 'Volume', width: 100, align: 'right' as const },
    { key: 'delta', label: 'Δ', width: 70, align: 'right' as const },
    { key: 'gamma', label: 'Γ', width: 70, align: 'right' as const },
    { key: 'theta', label: 'Θ', width: 70, align: 'right' as const },
    { key: 'vega', label: 'ν', width: 70, align: 'right' as const },
    { key: 'actions', label: '', width: 40 },
  ];

  if (isLoading) {
    return (
      <div className="h-full flex items-center justify-center text-foreground-muted">
        Loading instruments...
      </div>
    );
  }

  if (instruments.length === 0) {
    return (
      <div className="h-full flex items-center justify-center text-foreground-muted">
        No instruments match your filters
      </div>
    );
  }

  return (
    <div className="h-full flex flex-col">
      {/* Header */}
      <div className="flex items-center border-b border-border bg-background-secondary sticky top-0 z-10">
        {columns.map((col) => (
          <div
            key={col.key}
            className={cn(
              'px-3 py-2 text-xs font-medium text-foreground-muted shrink-0',
              col.align === 'right' && 'text-right',
              col.align === 'center' && 'text-center'
            )}
            style={{ width: col.width }}
          >
            <button className="inline-flex items-center gap-1 hover:text-foreground transition-colors">
              {col.label}
              {col.label && <ArrowUpDown className="w-3 h-3 opacity-50" />}
            </button>
          </div>
        ))}
      </div>

      {/* Virtualized Body */}
      <div ref={parentRef} className="flex-1 overflow-auto">
        <div
          style={{
            height: `${rowVirtualizer.getTotalSize()}px`,
            width: '100%',
            position: 'relative',
          }}
        >
          {rowVirtualizer.getVirtualItems().map((virtualRow) => {
            const instrument = instruments[virtualRow.index];
            const isPositiveOiChange = (instrument.oi_change || 0) >= 0;

            return (
              <div
                key={instrument.tradingsymbol}
                className="absolute top-0 left-0 w-full flex items-center border-b border-border/50 hover:bg-background-tertiary/50 transition-colors"
                style={{
                  height: `${virtualRow.size}px`,
                  transform: `translateY(${virtualRow.start}px)`,
                }}
              >
                {/* Symbol */}
                <div className="px-3 py-2 shrink-0" style={{ width: columns[0].width }}>
                  <span className="font-mono text-sm font-medium">{instrument.tradingsymbol}</span>
                </div>

                {/* Strike */}
                <div className="px-3 py-2 text-right shrink-0" style={{ width: columns[1].width }}>
                  <span className="font-mono text-sm">{formatNumber(instrument.strike, 0)}</span>
                </div>

                {/* Type */}
                <div className="px-3 py-2 text-center shrink-0" style={{ width: columns[2].width }}>
                  <span
                    className={cn(
                      'px-1.5 py-0.5 rounded text-xs font-medium',
                      instrument.option_type === 'CE'
                        ? 'bg-profit/10 text-profit'
                        : 'bg-loss/10 text-loss'
                    )}
                  >
                    {instrument.option_type}
                  </span>
                </div>

                {/* LTP */}
                <div className="px-3 py-2 text-right shrink-0" style={{ width: columns[3].width }}>
                  <span className="font-mono text-sm">{formatCurrency(instrument.last_price)}</span>
                </div>

                {/* OI Change */}
                <div className="px-3 py-2 text-right shrink-0" style={{ width: columns[4].width }}>
                  <span
                    className={cn(
                      'font-mono text-sm',
                      isPositiveOiChange ? 'text-profit' : 'text-loss'
                    )}
                  >
                    {isPositiveOiChange ? '+' : ''}{formatNumber(instrument.oi_change || 0, 0)}
                  </span>
                </div>

                {/* IV */}
                <div className="px-3 py-2 text-right shrink-0" style={{ width: columns[5].width }}>
                  <span className="font-mono text-sm">{((instrument.iv || 0) * 100).toFixed(1)}%</span>
                </div>

                {/* OI */}
                <div className="px-3 py-2 text-right shrink-0" style={{ width: columns[6].width }}>
                  <span className="font-mono text-sm">{((instrument.open_interest || 0) / 1000).toFixed(1)}K</span>
                </div>

                {/* Volume */}
                <div className="px-3 py-2 text-right shrink-0" style={{ width: columns[7].width }}>
                  <span className="font-mono text-sm">{((instrument.volume || 0) / 1000).toFixed(1)}K</span>
                </div>

                {/* Greeks */}
                <div className="px-3 py-2 text-right shrink-0" style={{ width: columns[8].width }}>
                  <span className="font-mono text-xs">{instrument.delta?.toFixed(3) || '-'}</span>
                </div>
                <div className="px-3 py-2 text-right shrink-0" style={{ width: columns[9].width }}>
                  <span className="font-mono text-xs">{instrument.gamma?.toFixed(4) || '-'}</span>
                </div>
                <div className="px-3 py-2 text-right shrink-0" style={{ width: columns[10].width }}>
                  <span className="font-mono text-xs">{instrument.theta?.toFixed(2) || '-'}</span>
                </div>
                <div className="px-3 py-2 text-right shrink-0" style={{ width: columns[11].width }}>
                  <span className="font-mono text-xs">{instrument.vega?.toFixed(2) || '-'}</span>
                </div>

                {/* Actions */}
                <div className="px-2 py-2 shrink-0" style={{ width: columns[12].width }}>
                  <button
                    onClick={() => onSelectInstrument(instrument)}
                    className="p-1 rounded hover:bg-background-tertiary text-foreground-muted hover:text-foreground transition-colors"
                  >
                    <ChevronRight className="w-4 h-4" />
                  </button>
                </div>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
