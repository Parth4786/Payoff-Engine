'use client';

import { cn } from '@/lib/utils';

interface Props {
  value: 'at-expiry' | 'now' | 'custom';
  onChange: (value: 'at-expiry' | 'now' | 'custom') => void;
  daysToExpiry: number;
  onDaysChange: (days: number) => void;
}

export function ScenarioTabs({ value, onChange, daysToExpiry, onDaysChange }: Props) {
  const tabs = [
    { id: 'at-expiry', label: 'At Expiry' },
    { id: 'now', label: 'Current' },
    { id: 'custom', label: 'Custom' },
  ] as const;

  return (
    <div className="flex items-center gap-3">
      <div className="flex items-center bg-background-tertiary rounded-lg p-1">
        {tabs.map((tab) => (
          <button
            key={tab.id}
            onClick={() => onChange(tab.id)}
            className={cn(
              'px-3 py-1.5 text-xs font-medium rounded-md transition-all',
              value === tab.id
                ? 'bg-background text-foreground shadow-sm'
                : 'text-foreground-muted hover:text-foreground'
            )}
          >
            {tab.label}
          </button>
        ))}
      </div>

      {value === 'custom' && (
        <div className="flex items-center gap-2">
          <input
            type="number"
            value={daysToExpiry}
            onChange={(e) => onDaysChange(Number(e.target.value))}
            className="w-14 px-2 py-1 text-xs font-mono bg-background-tertiary border border-border rounded focus:outline-none focus:border-accent"
            min={0}
            max={365}
          />
          <span className="text-xs text-foreground-muted">days</span>
        </div>
      )}
    </div>
  );
}
