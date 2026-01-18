'use client';

import { cn } from '@/lib/utils';

type GreekType = 'delta' | 'gamma' | 'theta' | 'vega';

interface Props {
  selectedGreeks: GreekType[];
  onToggleGreek: (greek: GreekType) => void;
  daysToExpiry: number;
  onDaysChange: (days: number) => void;
  ivShift: number;
  onIvShiftChange: (shift: number) => void;
}

const greekConfig: Record<GreekType, { label: string; description: string; color: string }> = {
  delta: {
    label: 'Delta (Δ)',
    description: 'Price sensitivity',
    color: 'bg-blue-500/20 text-blue-400 border-blue-500/30',
  },
  gamma: {
    label: 'Gamma (Γ)',
    description: 'Delta change rate',
    color: 'bg-purple-500/20 text-purple-400 border-purple-500/30',
  },
  theta: {
    label: 'Theta (Θ)',
    description: 'Time decay',
    color: 'bg-amber-500/20 text-amber-400 border-amber-500/30',
  },
  vega: {
    label: 'Vega (ν)',
    description: 'IV sensitivity',
    color: 'bg-emerald-500/20 text-emerald-400 border-emerald-500/30',
  },
};

export function SurfaceControls({
  selectedGreeks,
  onToggleGreek,
  daysToExpiry,
  onDaysChange,
  ivShift,
  onIvShiftChange,
}: Props) {
  const greeks: GreekType[] = ['delta', 'gamma', 'theta', 'vega'];

  return (
    <div className="flex flex-wrap items-center gap-6">
      {/* Greek Selection */}
      <div className="flex-1">
        <label className="text-xs text-foreground-muted block mb-2">Show Greeks</label>
        <div className="flex flex-wrap gap-2">
          {greeks.map((greek) => {
            const config = greekConfig[greek];
            const isSelected = selectedGreeks.includes(greek);
            
            return (
              <button
                key={greek}
                onClick={() => onToggleGreek(greek)}
                className={cn(
                  'px-3 py-2 rounded-lg border text-sm font-medium transition-all',
                  isSelected ? config.color : 'bg-background-tertiary/50 text-foreground-muted border-border'
                )}
              >
                {config.label}
              </button>
            );
          })}
        </div>
      </div>

      {/* Days to Expiry */}
      <div>
        <label className="text-xs text-foreground-muted block mb-2">Days to Expiry</label>
        <div className="flex items-center gap-2">
          <input
            type="range"
            min={0}
            max={30}
            value={daysToExpiry}
            onChange={(e) => onDaysChange(Number(e.target.value))}
            className="w-24 accent-accent"
          />
          <input
            type="number"
            value={daysToExpiry}
            onChange={(e) => onDaysChange(Number(e.target.value))}
            className="w-16 px-2 py-1 text-sm font-mono bg-background-tertiary border border-border rounded focus:outline-none focus:border-accent"
            min={0}
            max={365}
          />
          <span className="text-xs text-foreground-muted">days</span>
        </div>
      </div>

      {/* IV Shift */}
      <div>
        <label className="text-xs text-foreground-muted block mb-2">IV Shift</label>
        <div className="flex items-center gap-2">
          <button
            onClick={() => onIvShiftChange(ivShift - 5)}
            className="px-2 py-1 text-sm bg-background-tertiary border border-border rounded hover:border-accent transition-colors"
          >
            -5%
          </button>
          <span className="w-16 text-center font-mono text-sm">
            {ivShift >= 0 ? '+' : ''}{ivShift}%
          </span>
          <button
            onClick={() => onIvShiftChange(ivShift + 5)}
            className="px-2 py-1 text-sm bg-background-tertiary border border-border rounded hover:border-accent transition-colors"
          >
            +5%
          </button>
        </div>
      </div>
    </div>
  );
}
