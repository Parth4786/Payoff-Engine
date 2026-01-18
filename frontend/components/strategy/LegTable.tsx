'use client';

import { useState } from 'react';
import { Plus, Trash2, Copy, GripVertical, Edit2 } from 'lucide-react';
import { useStrategy } from '@/hooks';
import { cn, formatCurrency } from '@/lib/utils';
import type { OptionLeg, OptionType, Side } from '@/lib/types';

export function LegTable() {
  const { strategy, addLeg, updateLeg, removeLeg, duplicateLeg } = useStrategy();
  const [editingId, setEditingId] = useState<string | null>(null);

  const handleAddLeg = () => {
    const defaultLeg: Omit<OptionLeg, 'id'> = {
      type: 'CE',
      side: 'BUY',
      strike: 26300,
      qty: 1,
      lot: 25,
      premium: 0,
    };
    addLeg(defaultLeg);
  };

  if (strategy.legs.length === 0) {
    return (
      <div className="text-center py-12">
        <p className="text-foreground-muted mb-4">No legs added yet</p>
        <button
          onClick={handleAddLeg}
          className="inline-flex items-center gap-2 px-4 py-2 bg-accent/10 text-accent rounded-lg hover:bg-accent/20 transition-colors"
        >
          <Plus className="w-4 h-4" />
          Add First Leg
        </button>
      </div>
    );
  }

  return (
    <div className="space-y-3">
      {strategy.legs.map((leg, index) => (
        <LegRow
          key={leg.id}
          leg={leg}
          index={index}
          isEditing={editingId === leg.id}
          onEdit={() => setEditingId(editingId === leg.id ? null : leg.id!)}
          onUpdate={(updates) => updateLeg(leg.id!, updates)}
          onRemove={() => removeLeg(leg.id!)}
          onDuplicate={() => duplicateLeg(leg.id!)}
        />
      ))}
      
      <button
        onClick={handleAddLeg}
        className="w-full flex items-center justify-center gap-2 px-4 py-3 border-2 border-dashed border-border rounded-lg text-foreground-muted hover:border-accent hover:text-accent transition-colors"
      >
        <Plus className="w-4 h-4" />
        Add Leg
      </button>
    </div>
  );
}

interface LegRowProps {
  leg: OptionLeg;
  index: number;
  isEditing: boolean;
  onEdit: () => void;
  onUpdate: (updates: Partial<OptionLeg>) => void;
  onRemove: () => void;
  onDuplicate: () => void;
}

function LegRow({ leg, index, isEditing, onEdit, onUpdate, onRemove, onDuplicate }: LegRowProps) {
  const isBuy = leg.side === 'BUY';
  const totalPremium = leg.premium * leg.qty * leg.lot;
  
  return (
    <div
      className={cn(
        'group relative border rounded-lg overflow-hidden transition-all',
        isBuy ? 'border-profit/30 bg-profit/5' : 'border-loss/30 bg-loss/5'
      )}
    >
      {/* Main Row */}
      <div className="flex items-center gap-3 p-3">
        {/* Drag Handle */}
        <div className="cursor-grab opacity-0 group-hover:opacity-50 hover:opacity-100 transition-opacity">
          <GripVertical className="w-4 h-4 text-foreground-muted" />
        </div>

        {/* Side Indicator */}
        <div
          className={cn(
            'w-2 h-8 rounded-full',
            isBuy ? 'bg-profit' : 'bg-loss'
          )}
        />

        {/* Leg Details */}
        <div className="flex-1 flex items-center gap-4">
          {/* Side Select */}
          <select
            value={leg.side}
            onChange={(e) => onUpdate({ side: e.target.value as Side })}
            className={cn(
              'px-2 py-1 rounded font-medium text-sm bg-transparent border-none focus:outline-none cursor-pointer',
              isBuy ? 'text-profit' : 'text-loss'
            )}
          >
            <option value="BUY">BUY</option>
            <option value="SELL">SELL</option>
          </select>

          {/* Type Select */}
          <select
            value={leg.type}
            onChange={(e) => onUpdate({ type: e.target.value as OptionType })}
            className="px-2 py-1 rounded text-sm bg-background-tertiary border border-border focus:outline-none focus:border-accent"
          >
            <option value="CE">CE</option>
            <option value="PE">PE</option>
          </select>

          {/* Strike */}
          <div className="flex items-center gap-1">
            <span className="text-xs text-foreground-muted">Strike:</span>
            <input
              type="number"
              value={leg.strike}
              onChange={(e) => onUpdate({ strike: Number(e.target.value) })}
              className="w-20 px-2 py-1 text-sm font-mono bg-background-tertiary border border-border rounded focus:outline-none focus:border-accent"
              step={50}
            />
          </div>

          {/* Qty */}
          <div className="flex items-center gap-1">
            <span className="text-xs text-foreground-muted">×</span>
            <input
              type="number"
              value={leg.qty}
              onChange={(e) => onUpdate({ qty: Number(e.target.value) })}
              className="w-12 px-2 py-1 text-sm font-mono bg-background-tertiary border border-border rounded focus:outline-none focus:border-accent"
              min={1}
            />
          </div>

          {/* Premium */}
          <div className="flex items-center gap-1">
            <span className="text-xs text-foreground-muted">@</span>
            <span className="text-foreground-muted">₹</span>
            <input
              type="number"
              value={leg.premium}
              onChange={(e) => onUpdate({ premium: Number(e.target.value) })}
              className="w-20 px-2 py-1 text-sm font-mono bg-background-tertiary border border-border rounded focus:outline-none focus:border-accent"
              step={0.05}
            />
          </div>

          {/* Total */}
          <div className="ml-auto flex items-center gap-2">
            <span
              className={cn(
                'text-sm font-mono font-medium',
                isBuy ? 'text-loss' : 'text-profit'
              )}
            >
              {isBuy ? '-' : '+'}
              {formatCurrency(Math.abs(totalPremium))}
            </span>
          </div>
        </div>

        {/* Actions */}
        <div className="flex items-center gap-1 opacity-0 group-hover:opacity-100 transition-opacity">
          <button
            onClick={onDuplicate}
            className="p-1.5 rounded hover:bg-background-tertiary text-foreground-muted hover:text-foreground transition-colors"
            title="Duplicate"
          >
            <Copy className="w-4 h-4" />
          </button>
          <button
            onClick={onRemove}
            className="p-1.5 rounded hover:bg-loss/10 text-foreground-muted hover:text-loss transition-colors"
            title="Remove"
          >
            <Trash2 className="w-4 h-4" />
          </button>
        </div>
      </div>
    </div>
  );
}
