'use client';

import { cn } from '@/lib/utils';
import { CheckCircle, XCircle, AlertTriangle, Clock, Wifi, Database, Server } from 'lucide-react';

interface Props {
  isConnected: boolean;
}

export function SystemHealth({ isConnected }: Props) {
  const healthChecks = [
    {
      name: 'WebSocket',
      status: isConnected ? 'healthy' : 'error',
      icon: Wifi,
      detail: isConnected ? 'Connected' : 'Disconnected',
    },
    {
      name: 'Backend API',
      status: 'healthy' as const,
      icon: Server,
      detail: 'localhost:8080',
    },
    {
      name: 'ClickHouse',
      status: 'healthy' as const,
      icon: Database,
      detail: 'Connected',
    },
    {
      name: 'Market Data',
      status: 'warning' as const,
      icon: Clock,
      detail: 'Delayed 2s',
    },
  ];

  return (
    <div className="flex items-center justify-between">
      <div className="flex items-center gap-6">
        {healthChecks.map((check) => {
          const Icon = check.icon;
          const statusConfig = {
            healthy: { color: 'text-profit', bg: 'bg-profit/10' },
            warning: { color: 'text-warning', bg: 'bg-warning/10' },
            error: { color: 'text-loss', bg: 'bg-loss/10' },
          }[check.status] ?? { color: 'text-foreground-muted', bg: 'bg-background-tertiary' };

          return (
            <div key={check.name} className="flex items-center gap-2">
              <div className={cn('p-1.5 rounded-lg', statusConfig.bg)}>
                <Icon className={cn('w-4 h-4', statusConfig.color)} />
              </div>
              <div>
                <div className="text-xs font-medium">{check.name}</div>
                <div className="text-xs text-foreground-muted">{check.detail}</div>
              </div>
            </div>
          );
        })}
      </div>

      {/* Last Update */}
      <div className="text-xs text-foreground-muted">
        Last update: <span className="font-mono">just now</span>
      </div>
    </div>
  );
}
