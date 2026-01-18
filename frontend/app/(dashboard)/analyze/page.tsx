'use client';

import { useState } from 'react';
import { Navbar, GreeksDisplay, PnLDisplay } from '@/components/shared';
import { PayoffChart } from '@/components/payoff/PayoffChart';
import { ScenarioTabs } from '@/components/payoff/ScenarioTabs';
import { PayoffTable } from '@/components/payoff/PayoffTable';
import { KeyMetrics } from '@/components/payoff/KeyMetrics';
import { useStrategy } from '@/hooks';
import { Settings, Download } from 'lucide-react';
import Link from 'next/link';

export default function AnalyzePage() {
  const { strategy, payoffResult, aggregateGreeks, isCalculating } = useStrategy();
  const [scenario, setScenario] = useState<'at-expiry' | 'now' | 'custom'>('at-expiry');
  const [daysToExpiry, setDaysToExpiry] = useState(7);

  return (
    <div className="min-h-screen">
      <Navbar
        title="Payoff Analysis"
        subtitle={strategy.underlying}
        actions={
          <div className="flex items-center gap-2">
            <button className="p-2 rounded-lg bg-background-tertiary hover:bg-background text-foreground-muted hover:text-foreground transition-colors">
              <Download className="w-4 h-4" />
            </button>
            <Link
              href="/sensitivity"
              className="flex items-center gap-1.5 px-4 py-2 bg-accent text-white rounded-lg text-sm font-medium hover:bg-accent/90 transition-colors"
            >
              Sensitivity Maps
            </Link>
          </div>
        }
      />

      <div className="p-6 space-y-6">
        {/* Top Section - Chart & Metrics */}
        <div className="grid grid-cols-1 xl:grid-cols-3 gap-6">
          {/* Payoff Chart - 2 cols */}
          <div className="xl:col-span-2 card">
            <div className="card-header flex items-center justify-between">
              <h2 className="card-title flex items-center gap-2">
                <span>📈</span>
                Payoff Diagram
              </h2>
              <ScenarioTabs
                value={scenario}
                onChange={setScenario}
                daysToExpiry={daysToExpiry}
                onDaysChange={setDaysToExpiry}
              />
            </div>
            <div className="card-content">
              <PayoffChart
                scenario={scenario}
                daysToExpiry={scenario === 'custom' ? daysToExpiry : undefined}
              />
            </div>
          </div>

          {/* Key Metrics - 1 col */}
          <div className="card">
            <div className="card-header">
              <h2 className="card-title">Key Metrics</h2>
            </div>
            <div className="card-content">
              <KeyMetrics />
            </div>
          </div>
        </div>

        {/* Greeks Section */}
        {aggregateGreeks && (
          <div className="card">
            <div className="card-header">
              <h2 className="card-title">Aggregate Greeks</h2>
            </div>
            <div className="card-content">
              <GreeksDisplay greeks={aggregateGreeks} showRho />
            </div>
          </div>
        )}

        {/* Payoff Table */}
        <div className="card">
          <div className="card-header flex items-center justify-between">
            <h2 className="card-title">Payoff at Spot Levels</h2>
            <button className="text-sm text-foreground-muted hover:text-foreground flex items-center gap-1">
              <Settings className="w-4 h-4" />
              Configure
            </button>
          </div>
          <div className="card-content p-0">
            <PayoffTable scenario={scenario} daysToExpiry={daysToExpiry} />
          </div>
        </div>
      </div>
    </div>
  );
}
