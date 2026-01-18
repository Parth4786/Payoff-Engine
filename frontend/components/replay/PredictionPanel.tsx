'use client';

import { useState } from 'react';
import { cn, formatCurrency, formatNumber } from '@/lib/utils';
import { Brain, RefreshCw, TrendingUp, TrendingDown, AlertTriangle } from 'lucide-react';
import type { PredictionResponse } from '@/lib/types';

interface Props {
  prediction: PredictionResponse | null;
  onLoadPrediction: () => void;
}

export function PredictionPanel({ prediction, onLoadPrediction }: Props) {
  const [isLoading, setIsLoading] = useState(false);

  const handleLoad = async () => {
    setIsLoading(true);
    await onLoadPrediction();
    setIsLoading(false);
  };

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

  const isBullish = prediction.direction === 'bullish';
  const isNeutral = prediction.direction === 'neutral';

  return (
    <div className="space-y-4">
      {/* Direction Indicator */}
      <div
        className={cn(
          'p-4 rounded-lg text-center',
          isBullish && 'bg-profit/10 border border-profit/20',
          !isBullish && !isNeutral && 'bg-loss/10 border border-loss/20',
          isNeutral && 'bg-warning/10 border border-warning/20'
        )}
      >
        <div className="flex items-center justify-center gap-2 mb-2">
          {isBullish ? (
            <TrendingUp className="w-6 h-6 text-profit" />
          ) : isNeutral ? (
            <AlertTriangle className="w-6 h-6 text-warning" />
          ) : (
            <TrendingDown className="w-6 h-6 text-loss" />
          )}
          <span
            className={cn(
              'text-lg font-semibold capitalize',
              isBullish && 'text-profit',
              !isBullish && !isNeutral && 'text-loss',
              isNeutral && 'text-warning'
            )}
          >
            {prediction.direction}
          </span>
        </div>
        <div className="text-sm text-foreground-muted">
          Confidence: <span className="font-mono">{(prediction.confidence * 100).toFixed(0)}%</span>
        </div>
      </div>

      {/* Predicted Values */}
      <div className="space-y-3">
        <div className="flex items-center justify-between p-3 rounded-lg bg-background-tertiary/50">
          <span className="text-sm text-foreground-muted">Predicted Spot</span>
          <span className="font-mono font-semibold">
            {formatNumber(prediction.predicted_spot, 2)}
          </span>
        </div>
        
        <div className="flex items-center justify-between p-3 rounded-lg bg-background-tertiary/50">
          <span className="text-sm text-foreground-muted">Predicted P&L</span>
          <span
            className={cn(
              'font-mono font-semibold',
              prediction.predicted_pnl >= 0 ? 'text-profit' : 'text-loss'
            )}
          >
            {formatCurrency(prediction.predicted_pnl)}
          </span>
        </div>

        {prediction.predicted_iv !== undefined && (
          <div className="flex items-center justify-between p-3 rounded-lg bg-background-tertiary/50">
            <span className="text-sm text-foreground-muted">Predicted IV</span>
            <span className="font-mono">
              {(prediction.predicted_iv * 100).toFixed(1)}%
            </span>
          </div>
        )}
      </div>

      {/* Range Forecast */}
      {prediction.range && (
        <div className="p-3 rounded-lg bg-background-tertiary/50">
          <span className="text-xs text-foreground-muted block mb-2">Expected Range</span>
          <div className="flex items-center justify-between">
            <span className="text-sm text-loss font-mono">{formatNumber(prediction.range.low, 0)}</span>
            <div className="flex-1 mx-3 h-1 bg-background rounded-full overflow-hidden">
              <div className="h-full bg-gradient-to-r from-loss via-warning to-profit" />
            </div>
            <span className="text-sm text-profit font-mono">{formatNumber(prediction.range.high, 0)}</span>
          </div>
        </div>
      )}

      {/* Reasoning */}
      {prediction.reasoning && (
        <div className="p-3 rounded-lg bg-info/5 border border-info/10">
          <span className="text-xs text-info block mb-1">AI Reasoning</span>
          <p className="text-sm text-foreground-secondary">{prediction.reasoning}</p>
        </div>
      )}

      {/* Refresh */}
      <button
        onClick={handleLoad}
        disabled={isLoading}
        className="w-full flex items-center justify-center gap-2 px-3 py-2 text-sm text-foreground-muted hover:text-foreground bg-background-tertiary rounded-lg hover:bg-background transition-colors"
      >
        <RefreshCw className={cn('w-4 h-4', isLoading && 'animate-spin')} />
        Refresh Prediction
      </button>
    </div>
  );
}
