'use client';

import { useState } from 'react';
import { Navbar } from '@/components/shared';
import { LegTable } from '@/components/strategy/LegTable';
import { OptionChainPicker } from '@/components/strategy/OptionChainPicker';
import { StrategySummary } from '@/components/strategy/StrategySummary';
import { RiskWarnings } from '@/components/strategy/RiskWarnings';
import { GreeksDisplay } from '@/components/shared';
import { useStrategy } from '@/hooks';
import { Calculator, ChevronRight } from 'lucide-react';
import Link from 'next/link';

export default function StrategyPage() {
  const {
    strategy,
    payoffResult,
    isCalculating,
    error,
    aggregateGreeks,
    calculatePayoff,
  } = useStrategy();
  
  const [spotPrice, setSpotPrice] = useState(26300);

  const handleCalculate = () => {
    calculatePayoff(spotPrice);
  };

  return (
    <div className="min-h-screen">
      <Navbar
        title="Strategy Builder"
        subtitle={strategy.underlying}
        actions={
          <Link
            href="/analyze"
            className="flex items-center gap-1.5 px-4 py-2 bg-accent text-white rounded-lg text-sm font-medium hover:bg-accent/90 transition-colors"
          >
            Analyze
            <ChevronRight className="w-4 h-4" />
          </Link>
        }
      />

      <div className="p-6 space-y-6">
        {/* Main Grid */}
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
          {/* Left Column - Strategy Legs */}
          <div className="space-y-6">
            <div className="card">
              <div className="card-header flex items-center justify-between">
                <h2 className="card-title flex items-center gap-2">
                  <span className="text-accent">📊</span>
                  Strategy Legs
                </h2>
                <div className="flex items-center gap-2">
                  <label className="text-xs text-foreground-muted">Spot:</label>
                  <input
                    type="number"
                    value={spotPrice}
                    onChange={(e) => setSpotPrice(Number(e.target.value))}
                    className="w-24 px-2 py-1 text-sm font-mono bg-background-tertiary border border-border rounded focus:outline-none focus:ring-1 focus:ring-accent"
                  />
                </div>
              </div>
              <div className="card-content">
                <LegTable />
              </div>
            </div>

            {/* Strategy Summary */}
            <div className="card">
              <div className="card-content">
                <StrategySummary spotPrice={spotPrice} />
                
                {/* Calculate Button */}
                <button
                  onClick={handleCalculate}
                  disabled={strategy.legs.length === 0 || isCalculating}
                  className="mt-4 w-full flex items-center justify-center gap-2 px-4 py-3 bg-accent text-white rounded-lg font-medium hover:bg-accent/90 disabled:opacity-50 disabled:cursor-not-allowed transition-all"
                >
                  <Calculator className="w-5 h-5" />
                  {isCalculating ? 'Calculating...' : 'Calculate Payoff'}
                </button>

                {error && (
                  <p className="mt-2 text-sm text-loss">{error}</p>
                )}
              </div>
            </div>

            {/* Risk Warnings */}
            <div className="card">
              <div className="card-header">
                <h2 className="card-title flex items-center gap-2">
                  <span>⚠️</span>
                  Risk Warnings
                </h2>
              </div>
              <div className="card-content">
                <RiskWarnings />
              </div>
            </div>
          </div>

          {/* Right Column - Option Chain */}
          <div className="space-y-6">
            <div className="card">
              <div className="card-header">
                <h2 className="card-title flex items-center gap-2">
                  <span className="text-info">⛓️</span>
                  Option Chain - {strategy.underlying}
                </h2>
              </div>
              <div className="card-content p-0">
                <OptionChainPicker spotPrice={spotPrice} />
              </div>
            </div>
          </div>
        </div>

        {/* Greeks Section - Full Width */}
        {aggregateGreeks && (
          <div className="card">
            <div className="card-header">
              <h2 className="card-title">Strategy Greeks (Aggregate)</h2>
            </div>
            <div className="card-content">
              <GreeksDisplay greeks={aggregateGreeks} showRho />
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
