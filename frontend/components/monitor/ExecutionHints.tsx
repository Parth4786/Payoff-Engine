'use client';

import { useState, useEffect } from 'react';
import { cn } from '@/lib/utils';
import { Lightbulb, ArrowRight, TrendingUp, TrendingDown, AlertTriangle, Clock } from 'lucide-react';

interface Hint {
  id: string;
  type: 'opportunity' | 'risk' | 'timing';
  priority: 'high' | 'medium' | 'low';
  title: string;
  description: string;
  action?: string;
}

export function ExecutionHints() {
  const [hints, setHints] = useState<Hint[]>([
    {
      id: '1',
      type: 'opportunity',
      priority: 'high',
      title: 'IV Crush Expected',
      description: 'Post-event IV typically drops 15-20%. Consider exiting long vega positions.',
      action: 'Review Positions',
    },
    {
      id: '2',
      type: 'risk',
      priority: 'high',
      title: 'High Gamma Zone',
      description: 'Position delta will change rapidly as spot approaches ATM strikes.',
      action: 'Set Alerts',
    },
    {
      id: '3',
      type: 'timing',
      priority: 'medium',
      title: 'Theta Decay Accelerating',
      description: '3 days to expiry - consider rolling positions to next expiry.',
      action: 'View Options',
    },
    {
      id: '4',
      type: 'opportunity',
      priority: 'low',
      title: 'Bid-Ask Spread Tight',
      description: 'Good liquidity for 26300 CE/PE strikes. Optimal entry time.',
    },
  ]);

  const typeConfig = {
    opportunity: {
      icon: TrendingUp,
      color: 'text-profit',
      bg: 'bg-profit/10',
      border: 'border-profit/20',
    },
    risk: {
      icon: AlertTriangle,
      color: 'text-loss',
      bg: 'bg-loss/10',
      border: 'border-loss/20',
    },
    timing: {
      icon: Clock,
      color: 'text-warning',
      bg: 'bg-warning/10',
      border: 'border-warning/20',
    },
  };

  const priorityConfig = {
    high: 'border-l-4',
    medium: 'border-l-2',
    low: 'border-l',
  };

  return (
    <div className="space-y-3">
      {hints.map((hint) => {
        const config = typeConfig[hint.type];
        const Icon = config.icon;

        return (
          <div
            key={hint.id}
            className={cn(
              'p-3 rounded-lg border transition-colors hover:bg-background-tertiary/50',
              config.border,
              priorityConfig[hint.priority],
              hint.priority === 'high' && config.border.replace('/20', '/40')
            )}
          >
            <div className="flex items-start gap-3">
              <div className={cn('p-1.5 rounded', config.bg)}>
                <Icon className={cn('w-4 h-4', config.color)} />
              </div>
              <div className="flex-1 min-w-0">
                <h4 className="text-sm font-medium mb-1">{hint.title}</h4>
                <p className="text-xs text-foreground-muted mb-2">{hint.description}</p>
                {hint.action && (
                  <button className="flex items-center gap-1 text-xs text-accent hover:text-accent/80 transition-colors">
                    {hint.action}
                    <ArrowRight className="w-3 h-3" />
                  </button>
                )}
              </div>
            </div>
          </div>
        );
      })}

      {hints.length === 0 && (
        <div className="text-center py-8 text-foreground-muted">
          <Lightbulb className="w-8 h-8 mx-auto mb-2 opacity-50" />
          <p className="text-sm">No hints available</p>
        </div>
      )}
    </div>
  );
}
