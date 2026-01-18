'use client';

import { useEffect } from 'react';
import { X, RefreshCw } from 'lucide-react';
import { useOptionChain } from '@/hooks';
import { formatCurrency, formatNumber, cn } from '@/lib/utils';

interface Props {
  symbol: string;
  onClose: () => void;
}

export function OptionChainModal({ symbol, onClose }: Props) {
  const { data: chainData, isLoading, refetch } = useOptionChain(symbol);

  // Close on escape
  useEffect(() => {
    const handleEscape = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    window.addEventListener('keydown', handleEscape);
    return () => window.removeEventListener('keydown', handleEscape);
  }, [onClose]);

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center">
      {/* Backdrop */}
      <div
        className="absolute inset-0 bg-black/60 backdrop-blur-sm"
        onClick={onClose}
      />

      {/* Modal */}
      <div className="relative w-full max-w-5xl max-h-[90vh] bg-background-secondary rounded-xl shadow-2xl border border-border overflow-hidden">
        {/* Header */}
        <div className="flex items-center justify-between px-6 py-4 border-b border-border">
          <div>
            <h2 className="text-lg font-semibold">{symbol} Option Chain</h2>
            {chainData && (
              <p className="text-sm text-foreground-muted">
                Spot: {formatNumber(chainData.spot, 2)} | Max Pain: {formatNumber(chainData.max_pain, 0)} | PCR: {chainData.pcr_oi.toFixed(2)}
              </p>
            )}
          </div>
          <div className="flex items-center gap-2">
            <button
              onClick={() => refetch()}
              className="p-2 rounded-lg hover:bg-background-tertiary text-foreground-muted hover:text-foreground transition-colors"
            >
              <RefreshCw className={cn('w-4 h-4', isLoading && 'animate-spin')} />
            </button>
            <button
              onClick={onClose}
              className="p-2 rounded-lg hover:bg-background-tertiary text-foreground-muted hover:text-foreground transition-colors"
            >
              <X className="w-5 h-5" />
            </button>
          </div>
        </div>

        {/* Content */}
        <div className="max-h-[calc(90vh-8rem)] overflow-auto">
          {isLoading ? (
            <div className="flex items-center justify-center py-20">
              <RefreshCw className="w-8 h-8 text-accent animate-spin" />
            </div>
          ) : !chainData?.strikes.length ? (
            <div className="flex items-center justify-center py-20 text-foreground-muted">
              No option chain data available
            </div>
          ) : (
            <table className="w-full text-sm">
              <thead className="sticky top-0 bg-background-secondary z-10">
                <tr>
                  <th colSpan={5} className="px-3 py-2 text-center text-profit border-b border-r border-border">
                    CALLS
                  </th>
                  <th className="px-3 py-2 text-center border-b border-border bg-background-tertiary">
                    Strike
                  </th>
                  <th colSpan={5} className="px-3 py-2 text-center text-loss border-b border-l border-border">
                    PUTS
                  </th>
                </tr>
                <tr className="text-foreground-muted text-xs">
                  <th className="px-2 py-1 text-right border-b border-border">OI</th>
                  <th className="px-2 py-1 text-right border-b border-border">Volume</th>
                  <th className="px-2 py-1 text-right border-b border-border">IV</th>
                  <th className="px-2 py-1 text-right border-b border-border">LTP</th>
                  <th className="px-2 py-1 text-right border-b border-r border-border">Chg%</th>
                  <th className="px-2 py-1 text-center border-b border-border bg-background-tertiary"></th>
                  <th className="px-2 py-1 text-right border-b border-l border-border">Chg%</th>
                  <th className="px-2 py-1 text-right border-b border-border">LTP</th>
                  <th className="px-2 py-1 text-right border-b border-border">IV</th>
                  <th className="px-2 py-1 text-right border-b border-border">Volume</th>
                  <th className="px-2 py-1 text-right border-b border-border">OI</th>
                </tr>
              </thead>
              <tbody>
                {chainData.strikes.map((strike) => {
                  const isATM = strike.is_atm;
                  const isITMCall = chainData.spot > strike.strike;
                  const isITMPut = chainData.spot < strike.strike;

                  return (
                    <tr
                      key={strike.strike}
                      className={cn(
                        'hover:bg-background-tertiary/50 transition-colors',
                        isATM && 'bg-accent/5 border-y border-accent/20'
                      )}
                    >
                      {/* Call OI */}
                      <td className={cn('px-2 py-1.5 text-right font-mono', isITMCall && 'bg-profit/5')}>
                        {strike.call ? (strike.call.open_interest / 1e6).toFixed(2) + 'M' : '-'}
                      </td>
                      {/* Call Volume */}
                      <td className={cn('px-2 py-1.5 text-right font-mono', isITMCall && 'bg-profit/5')}>
                        {strike.call ? (strike.call.volume / 1e3).toFixed(1) + 'K' : '-'}
                      </td>
                      {/* Call IV */}
                      <td className={cn('px-2 py-1.5 text-right font-mono', isITMCall && 'bg-profit/5')}>
                        {strike.call ? (strike.call.iv * 100).toFixed(1) + '%' : '-'}
                      </td>
                      {/* Call LTP */}
                      <td className={cn('px-2 py-1.5 text-right font-mono font-medium', isITMCall && 'bg-profit/5')}>
                        {strike.call ? formatCurrency(strike.call.last_price) : '-'}
                      </td>
                      {/* Call Change (OI Change) */}
                      <td className={cn('px-2 py-1.5 text-right font-mono border-r border-border', isITMCall && 'bg-profit/5')}>
                        {strike.call ? (
                          <span className={strike.call.oi_change >= 0 ? 'text-profit' : 'text-loss'}>
                            {strike.call.oi_change >= 0 ? '+' : ''}{formatNumber(strike.call.oi_change / 1000, 1)}K
                          </span>
                        ) : '-'}
                      </td>

                      {/* Strike */}
                      <td className={cn('px-3 py-1.5 text-center font-mono font-semibold bg-background-tertiary', isATM && 'text-accent')}>
                        {strike.strike}
                        {isATM && <span className="ml-1 text-xs">ATM</span>}
                      </td>

                      {/* Put Change (OI Change) */}
                      <td className={cn('px-2 py-1.5 text-right font-mono border-l border-border', isITMPut && 'bg-loss/5')}>
                        {strike.put ? (
                          <span className={strike.put.oi_change >= 0 ? 'text-profit' : 'text-loss'}>
                            {strike.put.oi_change >= 0 ? '+' : ''}{formatNumber(strike.put.oi_change / 1000, 1)}K
                          </span>
                        ) : '-'}
                      </td>
                      {/* Put LTP */}
                      <td className={cn('px-2 py-1.5 text-right font-mono font-medium', isITMPut && 'bg-loss/5')}>
                        {strike.put ? formatCurrency(strike.put.last_price) : '-'}
                      </td>
                      {/* Put IV */}
                      <td className={cn('px-2 py-1.5 text-right font-mono', isITMPut && 'bg-loss/5')}>
                        {strike.put ? (strike.put.iv * 100).toFixed(1) + '%' : '-'}
                      </td>
                      {/* Put Volume */}
                      <td className={cn('px-2 py-1.5 text-right font-mono', isITMPut && 'bg-loss/5')}>
                        {strike.put ? (strike.put.volume / 1e3).toFixed(1) + 'K' : '-'}
                      </td>
                      {/* Put OI */}
                      <td className={cn('px-2 py-1.5 text-right font-mono', isITMPut && 'bg-loss/5')}>
                        {strike.put ? (strike.put.open_interest / 1e6).toFixed(2) + 'M' : '-'}
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          )}
        </div>
      </div>
    </div>
  );
}
