'use client';

import { useMemo } from 'react';
import { cn } from '@/lib/utils';
import { Clock, TrendingUp, TrendingDown, AlertTriangle, Calendar } from 'lucide-react';
import type { ReplaySnapshot } from '@/lib/types';

interface Props {
  snapshots: ReplaySnapshot[];
  currentIndex: number;
  onSeek: (index: number) => void;
}

interface MarketEvent {
  index: number;
  timestamp: number;
  type: 'spike' | 'drop' | 'high' | 'low' | 'opening' | 'closing';
  label: string;
  description: string;
}

export function EventTimeline({ snapshots, currentIndex, onSeek }: Props) {
  // Detect significant market events
  const events = useMemo(() => {
    if (snapshots.length < 10) return [];

    const detectedEvents: MarketEvent[] = [];
    
    snapshots.forEach((snap, index) => {
      if (index < 2) return;

      const prevSnap = snapshots[index - 1];
      const spotChange = ((snap.spot - prevSnap.spot) / prevSnap.spot) * 100;
      
      // Detect significant moves (>0.5%)
      if (Math.abs(spotChange) > 0.5) {
        detectedEvents.push({
          index,
          timestamp: snap.timestamp,
          type: spotChange > 0 ? 'spike' : 'drop',
          label: `${spotChange > 0 ? '+' : ''}${spotChange.toFixed(2)}% move`,
          description: `Spot ${spotChange > 0 ? 'jumped' : 'dropped'} from ${prevSnap.spot.toFixed(0)} to ${snap.spot.toFixed(0)}`,
        });
      }

      // Detect P&L extremes
      const maxPnl = Math.max(...snapshots.slice(0, index + 1).map((s) => s.pnl || 0));
      const minPnl = Math.min(...snapshots.slice(0, index + 1).map((s) => s.pnl || 0));
      
      if (snap.pnl === maxPnl && (snap.pnl || 0) > 0 && index > 0) {
        const existing = detectedEvents.find((e) => e.type === 'high' && e.index > index - 5);
        if (!existing) {
          detectedEvents.push({
            index,
            timestamp: snap.timestamp,
            type: 'high',
            label: 'P&L High',
            description: `Peak profit: ₹${(snap.pnl || 0).toFixed(0)}`,
          });
        }
      }

      if (snap.pnl === minPnl && (snap.pnl || 0) < 0 && index > 0) {
        const existing = detectedEvents.find((e) => e.type === 'low' && e.index > index - 5);
        if (!existing) {
          detectedEvents.push({
            index,
            timestamp: snap.timestamp,
            type: 'low',
            label: 'P&L Low',
            description: `Max drawdown: ₹${(snap.pnl || 0).toFixed(0)}`,
          });
        }
      }

      // Detect market open/close times (9:15 and 15:30 IST)
      const date = new Date(snap.timestamp);
      const hours = date.getHours();
      const minutes = date.getMinutes();
      
      if (hours === 9 && minutes >= 15 && minutes < 20) {
        const existing = detectedEvents.find((e) => e.type === 'opening' && e.index > index - 5);
        if (!existing) {
          detectedEvents.push({
            index,
            timestamp: snap.timestamp,
            type: 'opening',
            label: 'Market Open',
            description: 'Trading session started',
          });
        }
      }
    });

    return detectedEvents.slice(-15); // Limit to recent events
  }, [snapshots]);

  const getEventIcon = (type: MarketEvent['type']) => {
    switch (type) {
      case 'spike':
        return <TrendingUp className="w-4 h-4 text-profit" />;
      case 'drop':
        return <TrendingDown className="w-4 h-4 text-loss" />;
      case 'high':
        return <TrendingUp className="w-4 h-4 text-profit" />;
      case 'low':
        return <TrendingDown className="w-4 h-4 text-loss" />;
      case 'opening':
      case 'closing':
        return <Calendar className="w-4 h-4 text-accent" />;
      default:
        return <Clock className="w-4 h-4 text-foreground-muted" />;
    }
  };

  if (events.length === 0) {
    return (
      <div className="p-6 text-center text-foreground-muted text-sm">
        No significant events detected
      </div>
    );
  }

  return (
    <div className="max-h-[300px] overflow-auto">
      <div className="divide-y divide-border/50">
        {events.map((event) => {
          const time = new Date(event.timestamp).toLocaleTimeString('en-IN', {
            hour: '2-digit',
            minute: '2-digit',
          });
          const isCurrent = event.index === currentIndex;

          return (
            <button
              key={`${event.timestamp}-${event.type}`}
              onClick={() => onSeek(event.index)}
              className={cn(
                'w-full flex items-start gap-3 p-3 text-left hover:bg-background-tertiary/50 transition-colors',
                isCurrent && 'bg-accent/10'
              )}
            >
              <div className="mt-0.5">{getEventIcon(event.type)}</div>
              <div className="flex-1 min-w-0">
                <div className="flex items-center justify-between gap-2">
                  <span className="font-medium text-sm truncate">{event.label}</span>
                  <span className="text-xs text-foreground-muted shrink-0">{time}</span>
                </div>
                <p className="text-xs text-foreground-muted mt-0.5 truncate">
                  {event.description}
                </p>
              </div>
            </button>
          );
        })}
      </div>
    </div>
  );
}
