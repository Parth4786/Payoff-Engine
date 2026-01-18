'use client';

import { useState } from 'react';
import { ChevronDown, RotateCcw } from 'lucide-react';
import { cn } from '@/lib/utils';
import type { ScreenerFilters as FilterType } from '@/lib/types';

interface Props {
  filters: FilterType;
  onFiltersChange: (filters: Partial<FilterType>) => void;
  underlyings: string[];
}

export function ScreenerFilters({ filters, onFiltersChange, underlyings }: Props) {
  const [expandedSections, setExpandedSections] = useState<string[]>(['underlying', 'moneyness', 'greeks']);

  const toggleSection = (section: string) => {
    setExpandedSections((prev) =>
      prev.includes(section) ? prev.filter((s) => s !== section) : [...prev, section]
    );
  };

  const handleReset = () => {
    onFiltersChange({
      underlying: undefined,
      expiry_ms: undefined,
      option_type: undefined,
      min_iv: undefined,
      max_iv: undefined,
      min_oi: undefined,
      min_volume: undefined,
      min_delta: undefined,
      max_delta: undefined,
      moneyness: undefined,
    });
  };

  return (
    <div className="space-y-4">
      {/* Header */}
      <div className="flex items-center justify-between">
        <h3 className="font-semibold">Filters</h3>
        <button
          onClick={handleReset}
          className="flex items-center gap-1 text-xs text-foreground-muted hover:text-foreground transition-colors"
        >
          <RotateCcw className="w-3 h-3" />
          Reset
        </button>
      </div>

      {/* Underlying */}
      <FilterSection
        title="Underlying"
        expanded={expandedSections.includes('underlying')}
        onToggle={() => toggleSection('underlying')}
      >
        <div className="space-y-2">
          {underlyings.length === 0 && (
            <div className="text-sm text-foreground-muted">No underlyings available</div>
          )}
          {underlyings.map((u) => (
            <label key={u} className="flex items-center gap-2 cursor-pointer">
              <input
                type="radio"
                name="underlying"
                checked={filters.underlying === u}
                onChange={() => onFiltersChange({ underlying: u })}
                className="accent-accent"
              />
              <span className="text-sm">{u}</span>
            </label>
          ))}
        </div>
      </FilterSection>

      {/* Option Type */}
      <FilterSection
        title="Option Type"
        expanded={expandedSections.includes('type')}
        onToggle={() => toggleSection('type')}
      >
        <div className="flex gap-2">
          <button
            onClick={() => onFiltersChange({ option_type: filters.option_type === 'CE' ? undefined : 'CE' })}
            className={cn(
              'flex-1 px-3 py-2 rounded-lg text-sm font-medium transition-colors',
              filters.option_type === 'CE'
                ? 'bg-profit/20 text-profit'
                : 'bg-background-tertiary text-foreground-muted hover:text-foreground'
            )}
          >
            Calls
          </button>
          <button
            onClick={() => onFiltersChange({ option_type: filters.option_type === 'PE' ? undefined : 'PE' })}
            className={cn(
              'flex-1 px-3 py-2 rounded-lg text-sm font-medium transition-colors',
              filters.option_type === 'PE'
                ? 'bg-loss/20 text-loss'
                : 'bg-background-tertiary text-foreground-muted hover:text-foreground'
            )}
          >
            Puts
          </button>
        </div>
      </FilterSection>

      {/* Moneyness */}
      <FilterSection
        title="Moneyness"
        expanded={expandedSections.includes('moneyness')}
        onToggle={() => toggleSection('moneyness')}
      >
        <div className="space-y-2">
          {(['ALL', 'ITM', 'ATM', 'OTM'] as const).map((m) => (
            <label key={m} className="flex items-center gap-2 cursor-pointer">
              <input
                type="radio"
                name="moneyness"
                checked={filters.moneyness === m || (!filters.moneyness && m === 'ALL')}
                onChange={() => onFiltersChange({ moneyness: m === 'ALL' ? undefined : m })}
                className="accent-accent"
              />
              <span className="text-sm">{m}</span>
            </label>
          ))}
        </div>
      </FilterSection>

      {/* IV Range */}
      <FilterSection
        title="Implied Volatility"
        expanded={expandedSections.includes('iv')}
        onToggle={() => toggleSection('iv')}
      >
        <div className="space-y-3">
          <div>
            <label className="text-xs text-foreground-muted block mb-1">Min IV (%)</label>
            <input
              type="number"
              value={filters.min_iv ? filters.min_iv * 100 : ''}
              onChange={(e) => onFiltersChange({ min_iv: Number(e.target.value) / 100 || undefined })}
              placeholder="Any"
              className="w-full px-3 py-2 text-sm bg-background-tertiary border border-border rounded-lg focus:outline-none focus:border-accent"
            />
          </div>
          <div>
            <label className="text-xs text-foreground-muted block mb-1">Max IV (%)</label>
            <input
              type="number"
              value={filters.max_iv ? filters.max_iv * 100 : ''}
              onChange={(e) => onFiltersChange({ max_iv: Number(e.target.value) / 100 || undefined })}
              placeholder="Any"
              className="w-full px-3 py-2 text-sm bg-background-tertiary border border-border rounded-lg focus:outline-none focus:border-accent"
            />
          </div>
        </div>
      </FilterSection>

      {/* Greeks */}
      <FilterSection
        title="Greeks Filters"
        expanded={expandedSections.includes('greeks')}
        onToggle={() => toggleSection('greeks')}
      >
        <div className="space-y-3">
          <div className="grid grid-cols-2 gap-2">
            <div>
              <label className="text-xs text-foreground-muted block mb-1">Delta Min</label>
              <input
                type="number"
                value={filters.min_delta || ''}
                onChange={(e) => onFiltersChange({ min_delta: Number(e.target.value) || undefined })}
                placeholder="-1"
                className="w-full px-2 py-1.5 text-sm bg-background-tertiary border border-border rounded focus:outline-none focus:border-accent"
                step={0.1}
                min={-1}
                max={1}
              />
            </div>
            <div>
              <label className="text-xs text-foreground-muted block mb-1">Delta Max</label>
              <input
                type="number"
                value={filters.max_delta || ''}
                onChange={(e) => onFiltersChange({ max_delta: Number(e.target.value) || undefined })}
                placeholder="1"
                className="w-full px-2 py-1.5 text-sm bg-background-tertiary border border-border rounded focus:outline-none focus:border-accent"
                step={0.1}
                min={-1}
                max={1}
              />
            </div>
          </div>
          <div>
            <label className="text-xs text-foreground-muted block mb-1">Min OI (in thousands)</label>
            <input
              type="number"
              value={filters.min_oi ? filters.min_oi / 1000 : ''}
              onChange={(e) => onFiltersChange({ min_oi: Number(e.target.value) * 1000 || undefined })}
              placeholder="Any"
              className="w-full px-3 py-2 text-sm bg-background-tertiary border border-border rounded-lg focus:outline-none focus:border-accent"
            />
          </div>
          <div>
            <label className="text-xs text-foreground-muted block mb-1">Min Volume</label>
            <input
              type="number"
              value={filters.min_volume || ''}
              onChange={(e) => onFiltersChange({ min_volume: Number(e.target.value) || undefined })}
              placeholder="Any"
              className="w-full px-3 py-2 text-sm bg-background-tertiary border border-border rounded-lg focus:outline-none focus:border-accent"
            />
          </div>
        </div>
      </FilterSection>
    </div>
  );
}

interface FilterSectionProps {
  title: string;
  expanded: boolean;
  onToggle: () => void;
  children: React.ReactNode;
}

function FilterSection({ title, expanded, onToggle, children }: FilterSectionProps) {
  return (
    <div className="border border-border rounded-lg overflow-hidden">
      <button
        onClick={onToggle}
        className="w-full flex items-center justify-between px-3 py-2 bg-background-tertiary/50 hover:bg-background-tertiary transition-colors"
      >
        <span className="text-sm font-medium">{title}</span>
        <ChevronDown
          className={cn(
            'w-4 h-4 text-foreground-muted transition-transform',
            expanded && 'rotate-180'
          )}
        />
      </button>
      {expanded && <div className="p-3">{children}</div>}
    </div>
  );
}
