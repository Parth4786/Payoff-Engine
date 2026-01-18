'use client';

import { useStrategy } from '@/hooks';
import { AlertTriangle, CheckCircle, Info, XCircle } from 'lucide-react';
import { cn } from '@/lib/utils';

type WarningLevel = 'success' | 'info' | 'warning' | 'danger';

interface Warning {
  level: WarningLevel;
  message: string;
}

export function RiskWarnings() {
  const { strategy, payoffResult, aggregateGreeks } = useStrategy();
  
  const warnings: Warning[] = [];
  
  // Check for naked positions (unlimited risk)
  if (payoffResult) {
    if (payoffResult.max_loss === -Infinity) {
      warnings.push({
        level: 'danger',
        message: 'Unlimited loss potential - naked position detected',
      });
    } else {
      warnings.push({
        level: 'success',
        message: 'Limited loss strategy',
      });
    }
    
    if (payoffResult.max_profit === Infinity) {
      warnings.push({
        level: 'info',
        message: 'Unlimited profit potential',
      });
    }
  }
  
  // Check Greeks-based warnings
  if (aggregateGreeks) {
    // High gamma warning (near expiry)
    if (Math.abs(aggregateGreeks.gamma) > 0.01) {
      warnings.push({
        level: 'warning',
        message: 'High gamma exposure - position is sensitive near ATM',
      });
    }
    
    // High theta decay
    if (aggregateGreeks.theta < -50) {
      warnings.push({
        level: 'warning',
        message: `Theta decay: ${Math.abs(aggregateGreeks.theta).toFixed(0)}/day - time is working against you`,
      });
    } else if (aggregateGreeks.theta > 50) {
      warnings.push({
        level: 'info',
        message: `Positive theta: +${aggregateGreeks.theta.toFixed(0)}/day - time decay benefits position`,
      });
    }
    
    // High vega exposure
    if (Math.abs(aggregateGreeks.vega) > 100) {
      warnings.push({
        level: 'info',
        message: 'Significant vega exposure - position is IV sensitive',
      });
    }
  }
  
  // Check for leg balance
  const buyLegs = strategy.legs.filter((l) => l.side === 'BUY');
  const sellLegs = strategy.legs.filter((l) => l.side === 'SELL');
  
  if (sellLegs.length > 0 && buyLegs.length === 0) {
    warnings.push({
      level: 'danger',
      message: 'Short-only position - consider hedging with long legs',
    });
  }
  
  // No legs warning
  if (strategy.legs.length === 0) {
    warnings.push({
      level: 'info',
      message: 'Add legs from the option chain to build your strategy',
    });
  }

  if (warnings.length === 0) {
    return (
      <div className="text-center py-4 text-foreground-muted text-sm">
        No warnings - calculate payoff to see risk analysis
      </div>
    );
  }

  return (
    <div className="space-y-2">
      {warnings.map((warning, i) => (
        <WarningItem key={i} level={warning.level} message={warning.message} />
      ))}
    </div>
  );
}

interface WarningItemProps {
  level: WarningLevel;
  message: string;
}

function WarningItem({ level, message }: WarningItemProps) {
  const config = {
    success: {
      icon: CheckCircle,
      bg: 'bg-profit/10',
      border: 'border-profit/20',
      text: 'text-profit',
    },
    info: {
      icon: Info,
      bg: 'bg-info/10',
      border: 'border-info/20',
      text: 'text-info',
    },
    warning: {
      icon: AlertTriangle,
      bg: 'bg-warning/10',
      border: 'border-warning/20',
      text: 'text-warning',
    },
    danger: {
      icon: XCircle,
      bg: 'bg-loss/10',
      border: 'border-loss/20',
      text: 'text-loss',
    },
  }[level];

  const Icon = config.icon;

  return (
    <div
      className={cn(
        'flex items-start gap-3 px-3 py-2.5 rounded-lg border',
        config.bg,
        config.border
      )}
    >
      <Icon className={cn('w-4 h-4 flex-shrink-0 mt-0.5', config.text)} />
      <span className="text-sm text-foreground-secondary">{message}</span>
    </div>
  );
}
