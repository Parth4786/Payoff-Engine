'use client';

import { useWebSocket } from '@/hooks';
import { cn } from '@/lib/utils';
import { Wifi, WifiOff, RefreshCw } from 'lucide-react';

interface Props {
  title?: string;
  subtitle?: string;
  actions?: React.ReactNode;
}

export function Navbar({ title, subtitle, actions }: Props) {
  const { status, isConnected, reconnect } = useWebSocket();

  return (
    <header className="h-14 bg-background-secondary border-b border-border px-6 flex items-center justify-between sticky top-0 z-30">
      <div className="flex items-center gap-4">
        {title && (
          <div>
            <h1 className="text-base font-semibold text-foreground">{title}</h1>
            {subtitle && (
              <p className="text-xs text-foreground-muted">{subtitle}</p>
            )}
          </div>
        )}
      </div>

      <div className="flex items-center gap-4">
        {/* Actions */}
        {actions}

        {/* Connection Status */}
        <div className="flex items-center gap-2">
          <div
            className={cn(
              'flex items-center gap-1.5 px-2.5 py-1 rounded-full text-xs font-medium',
              isConnected
                ? 'bg-profit/10 text-profit'
                : status === 'connecting'
                ? 'bg-warning/10 text-warning'
                : 'bg-loss/10 text-loss'
            )}
          >
            {isConnected ? (
              <>
                <Wifi className="w-3.5 h-3.5" />
                <span>Live</span>
              </>
            ) : status === 'connecting' ? (
              <>
                <RefreshCw className="w-3.5 h-3.5 animate-spin" />
                <span>Connecting...</span>
              </>
            ) : (
              <>
                <WifiOff className="w-3.5 h-3.5" />
                <button
                  onClick={reconnect}
                  className="hover:underline"
                >
                  Reconnect
                </button>
              </>
            )}
          </div>
        </div>
      </div>
    </header>
  );
}
