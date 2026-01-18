'use client';

import { useStrategy } from '@/hooks';
import { formatCurrency } from '@/lib/utils';
import { PnLDisplay } from '@/components/shared';

interface Props {
  spotPrice: number;
}

export function StrategySummary({ spotPrice }: Props) {
  const { strategy, payoffResult, netPremium } = useStrategy();
  
  const isDebit = netPremium < 0;
  
  return (
    <div className="space-y-4">
      {/* Strategy Name */}
      {payoffResult?.strategy && (
        <div className="flex items-center justify-between">
          <span className="text-sm text-foreground-muted">Strategy Type</span>
          <span className="text-sm font-semibold text-accent">{payoffResult.strategy}</span>
        </div>
      )}
      
      {/* Net Premium */}
      <div className="flex items-center justify-between py-3 border-y border-border">
        <span className="text-sm text-foreground-muted">
          {isDebit ? 'Net Debit' : 'Net Credit'}
        </span>
        <PnLDisplay
          value={netPremium}
          size="lg"
          showSign={false}
        />
      </div>
      
      {/* Max P&L */}
      {payoffResult && (
        <div className="grid grid-cols-2 gap-4">
          <div className="p-3 rounded-lg bg-profit/5 border border-profit/20">
            <span className="text-xs text-foreground-muted block mb-1">Max Profit</span>
            <span className="text-lg font-mono font-semibold text-profit">
              {payoffResult.max_profit === Infinity
                ? 'Unlimited'
                : formatCurrency(payoffResult.max_profit)}
            </span>
          </div>
          <div className="p-3 rounded-lg bg-loss/5 border border-loss/20">
            <span className="text-xs text-foreground-muted block mb-1">Max Loss</span>
            <span className="text-lg font-mono font-semibold text-loss">
              {payoffResult.max_loss === -Infinity
                ? 'Unlimited'
                : formatCurrency(Math.abs(payoffResult.max_loss))}
            </span>
          </div>
        </div>
      )}
      
      {/* Breakevens */}
      {payoffResult?.breakevens && payoffResult.breakevens.length > 0 && (
        <div>
          <span className="text-xs text-foreground-muted block mb-2">Breakeven(s)</span>
          <div className="flex flex-wrap gap-2">
            {payoffResult.breakevens.map((be, i) => (
              <span
                key={i}
                className="px-3 py-1.5 rounded-full bg-background-tertiary font-mono text-sm"
              >
                {formatCurrency(be, { decimals: 2, symbol: '' })}
              </span>
            ))}
          </div>
        </div>
      )}
      
      {/* Quick Stats */}
      {payoffResult && (
        <div className="grid grid-cols-3 gap-3 pt-3 border-t border-border">
          <div className="text-center">
            <span className="text-xs text-foreground-muted block">Current Spot</span>
            <span className="font-mono text-sm">{formatCurrency(spotPrice, { decimals: 2, symbol: '' })}</span>
          </div>
          <div className="text-center">
            <span className="text-xs text-foreground-muted block">Legs</span>
            <span className="font-mono text-sm">{strategy.legs.length}</span>
          </div>
          <div className="text-center">
            <span className="text-xs text-foreground-muted block">Risk/Reward</span>
            <span className="font-mono text-sm">
              {payoffResult.max_loss !== 0 && payoffResult.max_loss !== -Infinity
                ? `1:${Math.abs(payoffResult.max_profit / payoffResult.max_loss).toFixed(1)}`
                : '-'}
            </span>
          </div>
        </div>
      )}
    </div>
  );
}
