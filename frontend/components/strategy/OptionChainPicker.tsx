'use client';

import { useState, useMemo } from 'react';
import { useLatestTimestamp, useOptionChain, useStrategy, useScreener } from '@/hooks';
import { cn, formatCurrency, formatNumber } from '@/lib/utils';
import { Plus, RefreshCw } from 'lucide-react';
import type { OptionChainStrike, OptionType, Side } from '@/lib/types';

interface Props {
  spotPrice: number;
}

export function OptionChainPicker({ spotPrice }: Props) {
  const { strategy, addLeg } = useStrategy();
  const { expiries } = useScreener();
  const { data: latestTimestamp } = useLatestTimestamp(7, 60);
  const [selectedExpiry, setSelectedExpiry] = useState<number | undefined>();
  const effectiveExpiry = selectedExpiry ?? expiries[0]?.expiry_ms;
  
  const { data: chainData, isLoading, refetch } = useOptionChain(
    strategy.underlying,
    effectiveExpiry,
    latestTimestamp
  );

  const handleAddLeg = (strike: number, type: OptionType, side: Side, premium: number) => {
    addLeg({
      type,
      side,
      strike,
      qty: 1,
      lot: 25, // NIFTY default
      premium,
    });
  };

  // Filter strikes around ATM
  const visibleStrikes = useMemo(() => {
    if (!chainData?.strikes) return [];
    
    const atmIndex = chainData.strikes.findIndex((s: OptionChainStrike) => s.is_atm);
    const start = Math.max(0, atmIndex - 10);
    const end = Math.min(chainData.strikes.length, atmIndex + 11);
    
    return chainData.strikes.slice(start, end);
  }, [chainData?.strikes]);

  return (
    <div className="h-[600px] flex flex-col">
      {/* Header */}
      <div className="flex items-center justify-between px-4 py-3 border-b border-border">
        <div className="flex items-center gap-4">
          <div className="flex items-center gap-2">
            <label className="text-xs text-foreground-muted">Expiry:</label>
            <select
              value={selectedExpiry || ''}
              onChange={(e) => setSelectedExpiry(Number(e.target.value) || undefined)}
              className="px-2 py-1 text-sm bg-background-tertiary border border-border rounded focus:outline-none focus:border-accent"
            >
              <option value="">Nearest</option>
              {expiries.map((exp) => (
                <option key={exp.expiry_ms} value={exp.expiry_ms}>
                  {exp.label}
                </option>
              ))}
            </select>
          </div>
          
          {chainData && (
            <span className="text-sm text-foreground-secondary">
              Spot: <span className="font-mono text-foreground">{formatNumber(chainData.spot, 2)}</span>
            </span>
          )}
        </div>

        <button
          onClick={() => refetch()}
          className="p-1.5 rounded hover:bg-background-tertiary text-foreground-muted hover:text-foreground transition-colors"
          title="Refresh"
        >
          <RefreshCw className={cn('w-4 h-4', isLoading && 'animate-spin')} />
        </button>
      </div>

      {/* Chain Summary */}
      {chainData && (
        <div className="flex items-center gap-6 px-4 py-2 bg-background-tertiary/50 text-xs border-b border-border">
          <span>
            Max Pain: <span className="font-mono text-warning">{formatNumber(chainData.max_pain, 0)}</span>
          </span>
          <span>
            PCR: <span className="font-mono">{chainData.pcr_oi.toFixed(2)}</span>
          </span>
          <span>
            Call OI: <span className="font-mono">{(chainData.total_call_oi / 1e6).toFixed(1)}M</span>
          </span>
          <span>
            Put OI: <span className="font-mono">{(chainData.total_put_oi / 1e6).toFixed(1)}M</span>
          </span>
        </div>
      )}

      {/* Chain Table */}
      <div className="flex-1 overflow-auto">
        {isLoading ? (
          <div className="flex items-center justify-center h-full">
            <RefreshCw className="w-6 h-6 text-foreground-muted animate-spin" />
          </div>
        ) : !chainData?.strikes.length ? (
          <div className="flex items-center justify-center h-full text-foreground-muted">
            No option chain data available
          </div>
        ) : (
          <table className="w-full text-xs">
            <thead className="sticky top-0 bg-background-secondary z-10">
              <tr>
                <th colSpan={4} className="px-2 py-2 text-center text-profit border-b border-r border-border">
                  CALLS
                </th>
                <th className="px-2 py-2 text-center border-b border-border bg-background-tertiary">
                  Strike
                </th>
                <th colSpan={4} className="px-2 py-2 text-center text-loss border-b border-l border-border">
                  PUTS
                </th>
              </tr>
              <tr className="text-foreground-muted">
                <th className="px-2 py-1 text-right border-b border-border">OI</th>
                <th className="px-2 py-1 text-right border-b border-border">LTP</th>
                <th className="px-2 py-1 text-right border-b border-border">IV</th>
                <th className="px-2 py-1 text-center border-b border-r border-border">Add</th>
                <th className="px-2 py-1 text-center border-b border-border bg-background-tertiary"></th>
                <th className="px-2 py-1 text-center border-b border-l border-border">Add</th>
                <th className="px-2 py-1 text-right border-b border-border">IV</th>
                <th className="px-2 py-1 text-right border-b border-border">LTP</th>
                <th className="px-2 py-1 text-right border-b border-border">OI</th>
              </tr>
            </thead>
            <tbody>
              {visibleStrikes.map((strike) => (
                <ChainRow
                  key={strike.strike}
                  strike={strike}
                  spot={chainData.spot}
                  onAddLeg={handleAddLeg}
                />
              ))}
            </tbody>
          </table>
        )}
      </div>

      {/* Footer hint */}
      <div className="px-4 py-2 border-t border-border text-xs text-foreground-muted">
        Click + to add leg to strategy
      </div>
    </div>
  );
}

interface ChainRowProps {
  strike: OptionChainStrike;
  spot: number;
  onAddLeg: (strike: number, type: OptionType, side: Side, premium: number) => void;
}

function ChainRow({ strike, spot, onAddLeg }: ChainRowProps) {
  const isITMCall = strike.call && spot > strike.strike;
  const isITMPut = strike.put && spot < strike.strike;
  const isATM = strike.is_atm;

  return (
    <tr
      className={cn(
        'hover:bg-background-tertiary/50 transition-colors',
        isATM && 'bg-accent/5 border-y border-accent/20'
      )}
    >
      {/* Call side */}
      <td className={cn('px-2 py-1.5 text-right font-mono', isITMCall && 'bg-profit/5')}>
        {strike.call ? (strike.call.open_interest / 1e6).toFixed(1) + 'M' : '-'}
      </td>
      <td className={cn('px-2 py-1.5 text-right font-mono font-medium', isITMCall && 'bg-profit/5')}>
        {strike.call ? formatCurrency(strike.call.last_price) : '-'}
      </td>
      <td className={cn('px-2 py-1.5 text-right font-mono', isITMCall && 'bg-profit/5')}>
        {strike.call ? (strike.call.iv * 100).toFixed(1) + '%' : '-'}
      </td>
      <td className={cn('px-2 py-1.5 text-center border-r border-border', isITMCall && 'bg-profit/5')}>
        {strike.call && (
          <div className="flex items-center justify-center gap-0.5">
            <button
              onClick={() => onAddLeg(strike.strike, 'CE', 'BUY', strike.call!.last_price)}
              className="p-0.5 rounded bg-profit/10 text-profit hover:bg-profit/20 transition-colors"
              title="Buy CE"
            >
              <Plus className="w-3 h-3" />
            </button>
            <button
              onClick={() => onAddLeg(strike.strike, 'CE', 'SELL', strike.call!.last_price)}
              className="p-0.5 rounded bg-loss/10 text-loss hover:bg-loss/20 transition-colors"
              title="Sell CE"
            >
              <Plus className="w-3 h-3" />
            </button>
          </div>
        )}
      </td>

      {/* Strike */}
      <td
        className={cn(
          'px-3 py-1.5 text-center font-mono font-semibold bg-background-tertiary',
          isATM && 'text-accent'
        )}
      >
        {strike.strike}
        {isATM && <span className="ml-1 text-xs">ATM</span>}
      </td>

      {/* Put side */}
      <td className={cn('px-2 py-1.5 text-center border-l border-border', isITMPut && 'bg-loss/5')}>
        {strike.put && (
          <div className="flex items-center justify-center gap-0.5">
            <button
              onClick={() => onAddLeg(strike.strike, 'PE', 'BUY', strike.put!.last_price)}
              className="p-0.5 rounded bg-profit/10 text-profit hover:bg-profit/20 transition-colors"
              title="Buy PE"
            >
              <Plus className="w-3 h-3" />
            </button>
            <button
              onClick={() => onAddLeg(strike.strike, 'PE', 'SELL', strike.put!.last_price)}
              className="p-0.5 rounded bg-loss/10 text-loss hover:bg-loss/20 transition-colors"
              title="Sell PE"
            >
              <Plus className="w-3 h-3" />
            </button>
          </div>
        )}
      </td>
      <td className={cn('px-2 py-1.5 text-right font-mono', isITMPut && 'bg-loss/5')}>
        {strike.put ? (strike.put.iv * 100).toFixed(1) + '%' : '-'}
      </td>
      <td className={cn('px-2 py-1.5 text-right font-mono font-medium', isITMPut && 'bg-loss/5')}>
        {strike.put ? formatCurrency(strike.put.last_price) : '-'}
      </td>
      <td className={cn('px-2 py-1.5 text-right font-mono', isITMPut && 'bg-loss/5')}>
        {strike.put ? (strike.put.open_interest / 1e6).toFixed(1) + 'M' : '-'}
      </td>
    </tr>
  );
}
