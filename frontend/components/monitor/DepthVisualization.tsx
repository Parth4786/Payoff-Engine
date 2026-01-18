'use client';

import { useMarketStore } from '@/lib/store';
import { formatNumber, cn } from '@/lib/utils';

interface Props {
  symbol: string;
}

export function DepthVisualization({ symbol }: Props) {
  const { depth } = useMarketStore();
  const depthData = depth.get(symbol);

  // Mock depth data if none available
  const mockDepth = {
    bids: Array.from({ length: 5 }, (_, i) => ({
      price: 26300 - i * 10,
      size: Math.floor(Math.random() * 1000) + 100,
      orders: Math.floor(Math.random() * 50) + 10,
    })),
    asks: Array.from({ length: 5 }, (_, i) => ({
      price: 26310 + i * 10,
      size: Math.floor(Math.random() * 1000) + 100,
      orders: Math.floor(Math.random() * 50) + 10,
    })),
  };

  const data = depthData || mockDepth;

  // Calculate max size for width scaling
  const maxSize = Math.max(
    ...data.bids.map((b) => b.size),
    ...data.asks.map((a) => a.size)
  );

  return (
    <div className="grid grid-cols-2 gap-8">
      {/* Bids (Buy) */}
      <div>
        <div className="flex items-center justify-between mb-3">
          <h3 className="text-sm font-medium text-profit">Bids</h3>
          <span className="text-xs text-foreground-muted">
            Total: {data.bids.reduce((sum, b) => sum + b.size, 0).toLocaleString()}
          </span>
        </div>
        <div className="space-y-1">
          {data.bids.map((bid, i) => (
            <div key={i} className="relative flex items-center justify-between py-1.5 px-2 rounded">
              {/* Bar Background */}
              <div
                className="absolute right-0 top-0 h-full bg-profit/10 rounded-r"
                style={{ width: `${(bid.size / maxSize) * 100}%` }}
              />
              {/* Content */}
              <span className="relative font-mono text-sm">{formatNumber(bid.price, 2)}</span>
              <span className="relative font-mono text-sm text-profit">{bid.size.toLocaleString()}</span>
            </div>
          ))}
        </div>
      </div>

      {/* Asks (Sell) */}
      <div>
        <div className="flex items-center justify-between mb-3">
          <h3 className="text-sm font-medium text-loss">Asks</h3>
          <span className="text-xs text-foreground-muted">
            Total: {data.asks.reduce((sum, a) => sum + a.size, 0).toLocaleString()}
          </span>
        </div>
        <div className="space-y-1">
          {data.asks.map((ask, i) => (
            <div key={i} className="relative flex items-center justify-between py-1.5 px-2 rounded">
              {/* Bar Background */}
              <div
                className="absolute left-0 top-0 h-full bg-loss/10 rounded-l"
                style={{ width: `${(ask.size / maxSize) * 100}%` }}
              />
              {/* Content */}
              <span className="relative font-mono text-sm text-loss">{ask.size.toLocaleString()}</span>
              <span className="relative font-mono text-sm">{formatNumber(ask.price, 2)}</span>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
