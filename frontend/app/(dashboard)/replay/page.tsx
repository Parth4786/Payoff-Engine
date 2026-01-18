'use client';

import { useState } from 'react';
import { Navbar } from '@/components/shared';
import { TimelineScrubber } from '@/components/replay/TimelineScrubber';
import { MarketStatePanel } from '@/components/replay/MarketStatePanel';
import { PredictionPanel } from '@/components/replay/PredictionPanel';
import { PayoffComparisonChart } from '@/components/replay/PayoffComparisonChart';
import { DeviationTable } from '@/components/replay/DeviationTable';
import { EventTimeline } from '@/components/replay/EventTimeline';
import { InsightRecorder } from '@/components/replay/InsightRecorder';
import { ReplayControls } from '@/components/replay/ReplayControls';
import { useReplay, useStrategy } from '@/hooks';
import { History, RotateCcw, Download } from 'lucide-react';

export default function ReplayPage() {
  const { strategy } = useStrategy();
  const {
    session,
    snapshots,
    currentSnapshot,
    prediction,
    comparison,
    isPlaying,
    playbackSpeed,
    currentIndex,
    isLoading,
    error,
    initReplay,
    play,
    pause,
    seekTo,
    setSpeed,
    loadPrediction,
    compareReplay,
  } = useReplay();

  const [showPrediction, setShowPrediction] = useState(true);
  const [compareMode, setCompareMode] = useState(false);

  const handleInitReplay = async () => {
    if (strategy.legs.length === 0) return;
    
    // Default: replay last 7 days
    const end = Date.now();
    const start = end - 7 * 24 * 60 * 60 * 1000;
    
    await initReplay(
      strategy.underlying,
      start,
      end,
      5 * 60 * 1000 // 5-minute intervals
    );
  };

  return (
    <div className="min-h-screen flex flex-col">
      <Navbar
        title="Replay Mode"
        subtitle={session ? `${new Date(session.start_ts).toLocaleDateString()} - ${new Date(session.end_ts).toLocaleDateString()}` : 'Historical Analysis'}
        actions={
          <div className="flex items-center gap-2">
            <button
              onClick={() => setShowPrediction(!showPrediction)}
              className={`px-3 py-2 rounded-lg text-sm font-medium transition-colors ${
                showPrediction
                  ? 'bg-info/20 text-info'
                  : 'bg-background-tertiary text-foreground-muted'
              }`}
            >
              Predictions
            </button>
            <button
              onClick={() => setCompareMode(!compareMode)}
              className={`px-3 py-2 rounded-lg text-sm font-medium transition-colors ${
                compareMode
                  ? 'bg-accent/20 text-accent'
                  : 'bg-background-tertiary text-foreground-muted'
              }`}
            >
              Compare
            </button>
            <button className="p-2 rounded-lg bg-background-tertiary hover:bg-background text-foreground-muted hover:text-foreground transition-colors">
              <Download className="w-4 h-4" />
            </button>
          </div>
        }
      />

      {/* Main Content */}
      <div className="flex-1 p-6 space-y-6">
        {/* Session Not Started */}
        {!session && !isLoading && (
          <div className="card">
            <div className="card-content py-16 text-center">
              <History className="w-16 h-16 mx-auto text-foreground-muted mb-4" />
              <h2 className="text-xl font-semibold mb-2">Start a Replay Session</h2>
              <p className="text-foreground-muted mb-6 max-w-md mx-auto">
                Replay historical market data for your strategy. See how your P&L would have evolved and compare actual vs predicted outcomes.
              </p>
              <button
                onClick={handleInitReplay}
                disabled={strategy.legs.length === 0}
                className="px-6 py-3 bg-accent text-white rounded-lg font-medium hover:bg-accent/90 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
              >
                {strategy.legs.length === 0 ? 'Build a Strategy First' : 'Start Replay (Last 7 Days)'}
              </button>
              {error && (
                <p className="mt-4 text-sm text-loss">{error}</p>
              )}
            </div>
          </div>
        )}

        {/* Loading */}
        {isLoading && !session && (
          <div className="card">
            <div className="card-content py-16 text-center">
              <RotateCcw className="w-12 h-12 mx-auto text-accent animate-spin mb-4" />
              <p className="text-foreground-muted">Loading historical data...</p>
            </div>
          </div>
        )}

        {/* Active Session */}
        {session && snapshots.length > 0 && (
          <>
            {/* Timeline Section */}
            <div className="card">
              <div className="card-content">
                <TimelineScrubber
                  snapshots={snapshots}
                  currentIndex={currentIndex}
                  onSeek={seekTo}
                />
              </div>
            </div>

            {/* Playback Controls */}
            <div className="card">
              <div className="card-content">
                <ReplayControls
                  isPlaying={isPlaying}
                  speed={playbackSpeed}
                  onPlay={play}
                  onPause={pause}
                  onSpeedChange={setSpeed}
                  currentSnapshot={currentSnapshot}
                />
              </div>
            </div>

            {/* Main Grid */}
            <div className="grid grid-cols-1 xl:grid-cols-3 gap-6">
              {/* Market State - 2 cols */}
              <div className="xl:col-span-2 card">
                <div className="card-header">
                  <h2 className="card-title">Market State</h2>
                </div>
                <div className="card-content">
                  <MarketStatePanel snapshot={currentSnapshot} />
                </div>
              </div>

              {/* Prediction Panel - 1 col */}
              {showPrediction && (
                <div className="card">
                  <div className="card-header">
                    <h2 className="card-title">AI Prediction</h2>
                  </div>
                  <div className="card-content">
                    <PredictionPanel
                      prediction={prediction}
                      onLoadPrediction={() => loadPrediction(currentSnapshot?.timestamp || 0)}
                    />
                  </div>
                </div>
              )}
            </div>

            {/* Payoff Comparison Chart */}
            <div className="card">
              <div className="card-header flex items-center justify-between">
                <h2 className="card-title">Payoff Over Time</h2>
                {!comparison && (
                  <button
                    onClick={() => compareReplay()}
                    className="text-sm text-accent hover:text-accent/80 transition-colors"
                  >
                    Load Comparison
                  </button>
                )}
              </div>
              <div className="card-content">
                <PayoffComparisonChart
                  snapshots={snapshots}
                  currentIndex={currentIndex}
                  comparison={comparison}
                />
              </div>
            </div>

            {/* Bottom Grid */}
            <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
              {/* Deviation Table */}
              <div className="card">
                <div className="card-header">
                  <h2 className="card-title">P&L Deviations</h2>
                </div>
                <div className="card-content p-0">
                  <DeviationTable
                    snapshots={snapshots}
                    comparison={comparison}
                  />
                </div>
              </div>

              {/* Event Timeline */}
              <div className="card">
                <div className="card-header">
                  <h2 className="card-title">Market Events</h2>
                </div>
                <div className="card-content p-0">
                  <EventTimeline
                    snapshots={snapshots}
                    currentIndex={currentIndex}
                    onSeek={seekTo}
                  />
                </div>
              </div>
            </div>

            {/* Insight Recorder */}
            <div className="card">
              <div className="card-header">
                <h2 className="card-title flex items-center gap-2">
                  <span>💡</span>
                  Insights & Notes
                </h2>
              </div>
              <div className="card-content">
                <InsightRecorder currentSnapshot={currentSnapshot} />
              </div>
            </div>
          </>
        )}
      </div>
    </div>
  );
}
