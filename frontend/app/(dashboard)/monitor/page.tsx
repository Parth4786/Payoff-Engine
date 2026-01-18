'use client';

import { Navbar } from '@/components/shared';
import { ActiveStrategies } from '@/components/monitor/ActiveStrategies';
import { SystemHealth } from '@/components/monitor/SystemHealth';
import { DepthVisualization } from '@/components/monitor/DepthVisualization';
import { ExecutionHints } from '@/components/monitor/ExecutionHints';
import { useWebSocket, useStrategy } from '@/hooks';
import { Activity, Wifi, WifiOff } from 'lucide-react';
import Link from 'next/link';

export default function MonitorPage() {
  const { isConnected, lastMessage, connect, disconnect } = useWebSocket();
  const { strategy } = useStrategy();

  return (
    <div className="min-h-screen">
      <Navbar
        title="Live Monitor"
        subtitle="Real-time Position Tracking"
        actions={
          <div className="flex items-center gap-2">
            <button
              onClick={isConnected ? disconnect : connect}
              className={`flex items-center gap-1.5 px-3 py-2 rounded-lg text-sm font-medium transition-colors ${
                isConnected
                  ? 'bg-profit/20 text-profit'
                  : 'bg-loss/20 text-loss'
              }`}
            >
              {isConnected ? (
                <>
                  <Wifi className="w-4 h-4" />
                  Connected
                </>
              ) : (
                <>
                  <WifiOff className="w-4 h-4" />
                  Disconnected
                </>
              )}
            </button>
            <Link
              href="/strategy"
              className="flex items-center gap-1.5 px-4 py-2 bg-accent text-white rounded-lg text-sm font-medium hover:bg-accent/90 transition-colors"
            >
              Build Strategy
            </Link>
          </div>
        }
      />

      <div className="p-6 space-y-6">
        {/* System Health Bar */}
        <div className="card">
          <div className="card-content py-3">
            <SystemHealth isConnected={isConnected} />
          </div>
        </div>

        {/* Main Grid */}
        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
          {/* Active Strategies - 2 cols */}
          <div className="lg:col-span-2 card">
            <div className="card-header flex items-center justify-between">
              <h2 className="card-title flex items-center gap-2">
                <Activity className="w-4 h-4 text-accent" />
                Active Strategies
              </h2>
              <span className="text-xs text-foreground-muted">
                Real-time P&L updates
              </span>
            </div>
            <div className="card-content p-0">
              <ActiveStrategies />
            </div>
          </div>

          {/* Execution Hints - 1 col */}
          <div className="card">
            <div className="card-header">
              <h2 className="card-title flex items-center gap-2">
                <span>💡</span>
                Execution Hints
              </h2>
            </div>
            <div className="card-content">
              <ExecutionHints />
            </div>
          </div>
        </div>

        {/* Depth Visualization */}
        <div className="card">
          <div className="card-header">
            <h2 className="card-title">Market Depth - {strategy.underlying}</h2>
          </div>
          <div className="card-content">
            <DepthVisualization symbol={strategy.underlying} />
          </div>
        </div>
      </div>
    </div>
  );
}
