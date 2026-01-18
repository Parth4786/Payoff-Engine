'use client';

import { Play, Pause, SkipBack, SkipForward, FastForward, Rewind } from 'lucide-react';
import { cn, formatNumber, formatCurrency } from '@/lib/utils';
import type { ReplaySnapshot } from '@/lib/types';

interface Props {
  isPlaying: boolean;
  speed: number;
  onPlay: () => void;
  onPause: () => void;
  onSpeedChange: (speed: number) => void;
  currentSnapshot: ReplaySnapshot | null;
}

export function ReplayControls({
  isPlaying,
  speed,
  onPlay,
  onPause,
  onSpeedChange,
  currentSnapshot,
}: Props) {
  const speeds = [0.5, 1, 2, 4, 8];

  return (
    <div className="flex items-center justify-between">
      {/* Playback Controls */}
      <div className="flex items-center gap-2">
        <button
          className="p-2 rounded-lg bg-background-tertiary hover:bg-background text-foreground-muted hover:text-foreground transition-colors"
          title="Skip to start"
        >
          <SkipBack className="w-4 h-4" />
        </button>
        
        <button
          className="p-2 rounded-lg bg-background-tertiary hover:bg-background text-foreground-muted hover:text-foreground transition-colors"
          title="Rewind 10"
        >
          <Rewind className="w-4 h-4" />
        </button>

        <button
          onClick={isPlaying ? onPause : onPlay}
          className="p-3 rounded-lg bg-accent text-white hover:bg-accent/90 transition-colors"
          title={isPlaying ? 'Pause' : 'Play'}
        >
          {isPlaying ? (
            <Pause className="w-5 h-5" />
          ) : (
            <Play className="w-5 h-5" />
          )}
        </button>

        <button
          className="p-2 rounded-lg bg-background-tertiary hover:bg-background text-foreground-muted hover:text-foreground transition-colors"
          title="Forward 10"
        >
          <FastForward className="w-4 h-4" />
        </button>

        <button
          className="p-2 rounded-lg bg-background-tertiary hover:bg-background text-foreground-muted hover:text-foreground transition-colors"
          title="Skip to end"
        >
          <SkipForward className="w-4 h-4" />
        </button>
      </div>

      {/* Speed Control */}
      <div className="flex items-center gap-2">
        <span className="text-xs text-foreground-muted">Speed:</span>
        <div className="flex items-center bg-background-tertiary rounded-lg p-1">
          {speeds.map((s) => (
            <button
              key={s}
              onClick={() => onSpeedChange(s)}
              className={cn(
                'px-3 py-1 text-xs font-medium rounded-md transition-all',
                speed === s
                  ? 'bg-background text-foreground shadow-sm'
                  : 'text-foreground-muted hover:text-foreground'
              )}
            >
              {s}x
            </button>
          ))}
        </div>
      </div>

      {/* Current Snapshot Info */}
      {currentSnapshot && (
        <div className="flex items-center gap-6 text-sm">
          <div className="flex items-center gap-2">
            <span className="text-foreground-muted">Spot:</span>
            <span className="font-mono font-semibold">{formatNumber(currentSnapshot.spot, 2)}</span>
          </div>
          <div className="flex items-center gap-2">
            <span className="text-foreground-muted">P&L:</span>
            <span
              className={cn(
                'font-mono font-semibold',
                (currentSnapshot.pnl || 0) >= 0 ? 'text-profit' : 'text-loss'
              )}
            >
              {formatCurrency(currentSnapshot.pnl || 0)}
            </span>
          </div>
        </div>
      )}
    </div>
  );
}
