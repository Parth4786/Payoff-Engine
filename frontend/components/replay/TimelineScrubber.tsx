'use client';

import { useRef, useEffect, useMemo } from 'react';
import { cn, formatNumber } from '@/lib/utils';
import type { ReplaySnapshot } from '@/lib/types';

interface Props {
  snapshots: ReplaySnapshot[];
  currentIndex: number;
  onSeek: (index: number) => void;
}

export function TimelineScrubber({ snapshots, currentIndex, onSeek }: Props) {
  const trackRef = useRef<HTMLDivElement>(null);
  const isDragging = useRef(false);

  // Calculate progress percentage
  const progress = snapshots.length > 0 ? (currentIndex / (snapshots.length - 1)) * 100 : 0;

  // Format timestamp for display
  const formatTime = (ts: number) => {
    const date = new Date(ts);
    return date.toLocaleTimeString('en-IN', { hour: '2-digit', minute: '2-digit' });
  };

  const formatDate = (ts: number) => {
    const date = new Date(ts);
    return date.toLocaleDateString('en-IN', { month: 'short', day: 'numeric' });
  };

  // Generate tick marks for the timeline
  const tickMarks = useMemo(() => {
    if (snapshots.length < 2) return [];
    
    const marks: { position: number; label: string; isDateChange: boolean }[] = [];
    const totalDuration = snapshots[snapshots.length - 1].timestamp - snapshots[0].timestamp;
    let lastDate = '';

    // Add ~10 tick marks
    const step = Math.max(1, Math.floor(snapshots.length / 10));
    
    for (let i = 0; i < snapshots.length; i += step) {
      const snapshot = snapshots[i];
      const position = (i / (snapshots.length - 1)) * 100;
      const currentDate = formatDate(snapshot.timestamp);
      const isDateChange = currentDate !== lastDate;
      
      marks.push({
        position,
        label: isDateChange ? currentDate : formatTime(snapshot.timestamp),
        isDateChange,
      });
      
      lastDate = currentDate;
    }
    
    return marks;
  }, [snapshots]);

  // Handle click/drag on timeline
  const handleTrackInteraction = (clientX: number) => {
    if (!trackRef.current || snapshots.length === 0) return;
    
    const rect = trackRef.current.getBoundingClientRect();
    const position = (clientX - rect.left) / rect.width;
    const index = Math.round(position * (snapshots.length - 1));
    const clampedIndex = Math.max(0, Math.min(snapshots.length - 1, index));
    
    onSeek(clampedIndex);
  };

  const handleMouseDown = (e: React.MouseEvent) => {
    isDragging.current = true;
    handleTrackInteraction(e.clientX);
  };

  useEffect(() => {
    const handleMouseMove = (e: MouseEvent) => {
      if (isDragging.current) {
        handleTrackInteraction(e.clientX);
      }
    };

    const handleMouseUp = () => {
      isDragging.current = false;
    };

    document.addEventListener('mousemove', handleMouseMove);
    document.addEventListener('mouseup', handleMouseUp);

    return () => {
      document.removeEventListener('mousemove', handleMouseMove);
      document.removeEventListener('mouseup', handleMouseUp);
    };
  }, [snapshots]);

  const currentSnapshot = snapshots[currentIndex];

  return (
    <div className="space-y-4">
      {/* Current Time Display */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-4">
          <span className="text-2xl font-mono font-semibold">
            {currentSnapshot ? formatTime(currentSnapshot.timestamp) : '--:--'}
          </span>
          <span className="text-sm text-foreground-muted">
            {currentSnapshot ? formatDate(currentSnapshot.timestamp) : ''}
          </span>
        </div>
        <div className="text-sm text-foreground-muted">
          {currentIndex + 1} / {snapshots.length} snapshots
        </div>
      </div>

      {/* Timeline Track */}
      <div className="relative pt-4 pb-6">
        {/* Track Background */}
        <div
          ref={trackRef}
          className="timeline-track h-2 cursor-pointer"
          onMouseDown={handleMouseDown}
        >
          {/* Progress Fill */}
          <div
            className="absolute h-full bg-gradient-to-r from-accent to-accent/80 rounded-l-full transition-[width] duration-75"
            style={{ width: `${progress}%` }}
          />
          
          {/* Scrubber Handle */}
          <div
            className="timeline-scrubber"
            style={{ left: `${progress}%` }}
          >
            <div className="w-4 h-4 rounded-full bg-accent border-2 border-white shadow-lg" />
          </div>
        </div>

        {/* Tick Marks */}
        <div className="absolute w-full top-7">
          {tickMarks.map((tick, i) => (
            <div
              key={i}
              className="absolute flex flex-col items-center -translate-x-1/2"
              style={{ left: `${tick.position}%` }}
            >
              <div
                className={cn(
                  'w-px',
                  tick.isDateChange ? 'h-2 bg-foreground-muted' : 'h-1 bg-border'
                )}
              />
              <span
                className={cn(
                  'text-[10px] mt-1',
                  tick.isDateChange ? 'text-foreground-secondary font-medium' : 'text-foreground-muted'
                )}
              >
                {tick.label}
              </span>
            </div>
          ))}
        </div>
      </div>

      {/* Spot Price Indicator */}
      {currentSnapshot && (
        <div className="flex items-center justify-center gap-6 text-sm">
          <div className="flex items-center gap-2">
            <span className="text-foreground-muted">Spot:</span>
            <span className="font-mono font-semibold">
              {formatNumber(currentSnapshot.spot, 2)}
            </span>
          </div>
          {currentSnapshot.iv !== undefined && (
            <div className="flex items-center gap-2">
              <span className="text-foreground-muted">IV:</span>
              <span className="font-mono">
                {(currentSnapshot.iv * 100).toFixed(1)}%
              </span>
            </div>
          )}
          {currentSnapshot.pnl !== undefined && (
            <div className="flex items-center gap-2">
              <span className="text-foreground-muted">P&L:</span>
              <span
                className={cn(
                  'font-mono font-semibold',
                  currentSnapshot.pnl >= 0 ? 'text-profit' : 'text-loss'
                )}
              >
                ₹{formatNumber(currentSnapshot.pnl, 0)}
              </span>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
