'use client';

import { useState, useMemo } from 'react';
import { cn, formatCurrency, formatNumber } from '@/lib/utils';
import { Brain, RefreshCw, TrendingUp, TrendingDown, Calendar } from 'lucide-react';
import type { PredictionResponse } from '@/lib/types';

interface Props {
  prediction: PredictionResponse | null;
  onLoadPrediction: () => void;
}

export function PredictionPanel({ prediction, onLoadPrediction }: Props) {
  const [isLoading, setIsLoading] = useState(false);
  const [selectedHorizon, setSelectedHorizon] = useState(0);

  const handleLoad = async () => {
    setIsLoading(true);
    await onLoadPrediction();
    setIsLoading(false);
  };

  // Extract prediction data for selected horizon
  const horizonData = useMemo(() => {
    if (!prediction || !prediction.predictions.length) return null;
    
    const pred = prediction.predictions[selectedHorizon];
    if (!pred) return null;

    // Find max profit/loss from payoff curve
    const maxPnl = Math.max(...pred.predicted_payoff.map(p => p.pnl));
    const minPnl = Math.min(...pred.predicted_payoff.map(p => p.pnl));
    const isBullish = maxPnl > Math.abs(minPnl);

    return {
      horizon: pred.horizon,
      greeks: pred.predicted_greeks,
      maxPnl,
      minPnl,
      isBullish,
      payoffPoints: pred.predicted_payoff,
    };
  }, [prediction, selectedHorizon]);

  if (!prediction) {
    return (
      <div className="text-center py-8">
        <Brain className="w-12 h-12 mx-auto text-foreground-muted mb-4" />
        <p className="text-sm text-foreground-muted mb-4">
          Get AI predictions for the current market state
        </p>
        <button
          onClick={handleLoad}
          disabled={isLoading}
          className="px-4 py-2 bg-info/20 text-info rounded-lg text-sm font-medium hover:bg-info/30 disabled:opacity-50 transition-colors"
        >
          {isLoading ? (
            <>
              <RefreshCw className="w-4 h-4 inline animate-spin mr-2" />
              Loading...
            </>
          ) : (
            'Load Prediction'
          )}
        </button>
      </div>
    );
  }

  return (
    <div className="space-y-4">
      {/* Market State at Prediction Time */}
      <div className="p-3 rounded-lg bg-background-tertiary/50">
        <span className="text-xs text-foreground-muted block mb-2">Market State</span>
        <div className="grid grid-cols-2 gap-2 text-sm">
          <div>
            <span className="text-foreground-muted">Spot:</span>
            <span className="ml-1 font-mono">{formatNumber(prediction.market_state_at_time.underlying_price, 0)}</span>
          </div>
          <div>
            <span className="text-foreground-muted">IV:</span>
            <span className="ml-1 font-mono">{(prediction.market_state_at_time.atm_iv * 100).toFixed(1)}%</span>
          </div>
          <div className="col-span-2">
            <span className="text-foreground-muted">DTE:</span>
            <span className="ml-1 font-mono">{prediction.market_state_at_time.days_to_expiry.toFixed(1)} days</span>
          </div>
        </div>
      </div>

      {/* Horizon Selector */}
      {prediction.predictions.length > 1 && (
        <div className="flex gap-2">
          {prediction.predictions.map((pred, i) => (
            <button
              key={i}
              onClick={() => setSelectedHorizon(i)}
              className={cn(
                'flex-1 px-3 py-2 rounded-lg text-xs font-medium transition-colors',
                selectedHorizon === i
                  ? 'bg-accent text-white'
                  : 'bg-background-tertiary text-foreground-muted hover:text-foreground'
              )}
            >
              +{pred.horizon.days_forward}D
            </button>
          ))}
        </div>
      )}

      {/* Prediction Data */}
      {horizonData && (
        <>
          {/* Direction Indicator */}
          <div
            className={cn(
              'p-4 rounded-lg text-center',
              horizonData.isBullish ? 'bg-profit/10 border border-profit/20' : 'bg-loss/10 border border-loss/20'
            )}
          >
            <div className="flex items-center justify-center gap-2 mb-2">
              {horizonData.isBullish ? (
                <TrendingUp className="w-6 h-6 text-profit" />
              ) : (
                <TrendingDown className="w-6 h-6 text-loss" />
              )}
              <span
                className={cn(
                  'text-lg font-semibold',
                  horizonData.isBullish ? 'text-profit' : 'text-loss'
                )}
              >
                {horizonData.isBullish ? 'Bullish' : 'Bearish'}
              </span>
            </div>
            <div className="text-sm text-foreground-muted">
              Horizon: <span className="font-mono">{horizonData.horizon.days_forward} days</span>
            </div>
          </div>

          {/* Predicted P&L Range */}
          <div className="space-y-3">
            <div className="flex items-center justify-between p-3 rounded-lg bg-background-tertiary/50">
              <span className="text-sm text-foreground-muted">Max Profit</span>
              <span className="font-mono font-semibold text-profit">
                {formatCurrency(horizonData.maxPnl)}
              </span>
            </div>
            
            <div className="flex items-center justify-between p-3 rounded-lg bg-background-tertiary/50">
              <span className="text-sm text-foreground-muted">Max Loss</span>
              <span className="font-mono font-semibold text-loss">
                {formatCurrency(horizonData.minPnl)}
              </span>
            </div>
          </div>

          {/* Greeks */}
          <div className="p-3 rounded-lg bg-background-tertiary/50">
            <span className="text-xs text-foreground-muted block mb-2">Predicted Greeks</span>
            <div className="grid grid-cols-2 gap-2 text-xs">
              <div>
                <span className="text-foreground-muted">Δ:</span>
                <span className="ml-1 font-mono">{horizonData.greeks.delta.toFixed(3)}</span>
              </div>
              <div>
                <span className="text-foreground-muted">Γ:</span>
                <span className="ml-1 font-mono">{horizonData.greeks.gamma.toFixed(4)}</span>
              </div>
              <div>
                <span className="text-foreground-muted">Θ:</span>
                <span className="ml-1 font-mono">{horizonData.greeks.theta.toFixed(2)}</span>
              </div>
              <div>
                <span className="text-foreground-muted">ν:</span>
                <span className="ml-1 font-mono">{horizonData.greeks.vega.toFixed(2)}</span>
              </div>
            </div>
          </div>
        </>
      )}
    </div>
  );
}
