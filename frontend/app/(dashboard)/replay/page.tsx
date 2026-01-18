'use client';

import { useMemo, useState } from 'react';
import { Navbar } from '@/components/shared';
import { TimelineScrubber } from '@/components/replay/TimelineScrubber';
import { MarketStatePanel } from '@/components/replay/MarketStatePanel';
import { PredictionPanel } from '@/components/replay/PredictionPanel';
import { PayoffComparisonChart } from '@/components/replay/PayoffComparisonChart';
import { DeviationTable } from '@/components/replay/DeviationTable';
import { EventTimeline } from '@/components/replay/EventTimeline';
import { InsightRecorder } from '@/components/replay/InsightRecorder';
import { ReplayControls } from '@/components/replay/ReplayControls';
import { useReplay, useStrategy, useUnderlyings } from '@/hooks';
import { History, RotateCcw, Download } from 'lucide-react';

export default function ReplayPage() {
  const { strategy, setUnderlying } = useStrategy();
  const { data: underlyings = [], isLoading: isLoadingUnderlyings } = useUnderlyings();
  const {
    sessionId,
    startTimestamp,
    endTimestamp,
    snapshots,
    currentSnapshot,
    predictionData,
    comparisonData,
    status,
    speed,
    error,
    setError: storeSetError,
    initReplay: storeInitReplay,
    play,
    pause,
    seek,
    setSpeed,
    loadPrediction,
    loadComparison,
    getCurrentSnapshotIndex,
    isLoadingReplay,
  } = useReplay();

  // Derived state
  const session = sessionId && startTimestamp && endTimestamp 
    ? { id: sessionId, start_ts: startTimestamp, end_ts: endTimestamp }
    : null;
  const isPlaying = status === 'playing';
  const isLoading = status === 'loading' || isLoadingReplay;
  const currentIndex = Math.max(0, getCurrentSnapshotIndex());
  const prediction = predictionData;
  const comparison = comparisonData;

  const [showPrediction, setShowPrediction] = useState(true);
  const [compareMode, setCompareMode] = useState(false);

  const defaultRange = useMemo(() => {
    const end = Date.now();
    const start = end - 7 * 24 * 60 * 60 * 1000;
    return { start, end };
  }, []);

  const toLocalDateTimeValue = (ms: number) => {
    const d = new Date(ms);
    const tzOffsetMs = d.getTimezoneOffset() * 60 * 1000;
    return new Date(ms - tzOffsetMs).toISOString().slice(0, 16);
  };

  const [startValue, setStartValue] = useState(() => toLocalDateTimeValue(defaultRange.start));
  const [endValue, setEndValue] = useState(() => toLocalDateTimeValue(defaultRange.end));
  const [intervalMs, setIntervalMs] = useState<number>(5 * 60 * 1000);

  const handleInitReplay = async () => {
    storeSetError(null);
    if (strategy.legs.length === 0) return;

    const start = new Date(startValue).getTime();
    const end = new Date(endValue).getTime();
    if (!Number.isFinite(start) || !Number.isFinite(end) || start <= 0 || end <= 0) {
      storeSetError('Start and end timestamps are required');
      return;
    }
    if (start >= end) {
      storeSetError('Start timestamp must be before end timestamp');
      return;
    }

    await storeInitReplay(strategy, start, end, intervalMs);
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

              <div className="max-w-2xl mx-auto mb-6 text-left grid grid-cols-1 md:grid-cols-2 gap-4">
                <div>
                  <label className="text-xs text-foreground-muted block mb-1">Underlying</label>
                  <select
                    value={strategy.underlying}
                    onChange={(e) => setUnderlying(e.target.value)}
                    disabled={isLoadingUnderlyings}
                    className="w-full px-3 py-2 text-sm bg-background-tertiary border border-border rounded-lg focus:outline-none focus:border-accent"
                  >
                    {underlyings.map((u) => (
                      <option key={u.symbol} value={u.symbol}>
                        {u.symbol}
                      </option>
                    ))}
                  </select>
                </div>

                <div>
                  <label className="text-xs text-foreground-muted block mb-1">Interval</label>
                  <select
                    value={intervalMs}
                    onChange={(e) => setIntervalMs(Number(e.target.value))}
                    className="w-full px-3 py-2 text-sm bg-background-tertiary border border-border rounded-lg focus:outline-none focus:border-accent"
                  >
                    <option value={60_000}>1 minute</option>
                    <option value={5 * 60_000}>5 minutes</option>
                    <option value={15 * 60_000}>15 minutes</option>
                    <option value={60 * 60_000}>1 hour</option>
                  </select>
                </div>

                <div>
                  <label className="text-xs text-foreground-muted block mb-1">Start</label>
                  <input
                    type="datetime-local"
                    value={startValue}
                    onChange={(e) => setStartValue(e.target.value)}
                    className="w-full px-3 py-2 text-sm bg-background-tertiary border border-border rounded-lg focus:outline-none focus:border-accent"
                  />
                </div>

                <div>
                  <label className="text-xs text-foreground-muted block mb-1">End</label>
                  <input
                    type="datetime-local"
                    value={endValue}
                    onChange={(e) => setEndValue(e.target.value)}
                    className="w-full px-3 py-2 text-sm bg-background-tertiary border border-border rounded-lg focus:outline-none focus:border-accent"
                  />
                </div>
              </div>

              <button
                onClick={handleInitReplay}
                disabled={strategy.legs.length === 0}
                className="px-6 py-3 bg-accent text-white rounded-lg font-medium hover:bg-accent/90 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
              >
                {strategy.legs.length === 0 ? 'Build a Strategy First' : 'Start Replay'}
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
                  onSeek={(index) => {
                    const snapshot = snapshots[index];
                    if (snapshot) seek(snapshot.timestamp);
                  }}
                />
              </div>
            </div>

            {/* Playback Controls */}
            <div className="card">
              <div className="card-content">
                <ReplayControls
                  isPlaying={isPlaying}
                  speed={speed}
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
                      onLoadPrediction={loadPrediction}
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
                    onClick={() => loadComparison()}
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
                    onSeek={seek}
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
