# Payoff Engine - WebSocket Protocol

> **Purpose**: Documentation of the real-time WebSocket streaming protocol for frontend integration.

## Table of Contents

1. [Connection](#1-connection)
2. [Message Protocol](#2-message-protocol)
3. [Client → Server Messages](#3-client--server-messages)
4. [Server → Client Messages](#4-server--client-messages)
5. [TypeScript Client](#5-typescript-client)
6. [React Hook](#6-react-hook)
7. [Error Handling](#7-error-handling)

---

## 1. Connection

### 1.1 WebSocket URL

```
Development: ws://localhost:8080/ws
Production:  wss://api.payoff-engine.com/ws
```

### 1.2 Connection Flow

```
Client                                           Server
  │                                                │
  │──────────── WebSocket Connect ────────────────→│
  │                                                │
  │←─────────── Connection Accepted ──────────────│
  │             (HTTP 101 Upgrade)                 │
  │                                                │
  │──────────── {"action":"ping"} ────────────────→│
  │                                                │
  │←─────────── {"type":"pong"} ──────────────────│
  │                                                │
  │ ... ready for subscriptions ...                │
```

### 1.3 Connection Headers

```http
GET /ws HTTP/1.1
Host: localhost:8080
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
```

---

## 2. Message Protocol

All messages are **JSON-encoded text frames**.

### 2.1 Message Structure

**Client → Server:**
```typescript
interface ClientMessage {
  action: string;         // Command type
  request_id?: string;    // Optional ID for response correlation
  [key: string]: any;     // Additional payload
}
```

**Server → Client:**
```typescript
interface ServerMessage {
  type: string;           // Message type
  request_id?: string;    // Echoed from request
  [key: string]: any;     // Message payload
}
```

### 2.2 Request-Response Correlation

Use `request_id` to correlate responses:

```typescript
// Client sends:
{ "action": "subscribe", "symbols": ["NFO:49543"], "request_id": "sub-001" }

// Server responds:
{ "type": "subscribed", "symbols": ["NFO:49543"], "request_id": "sub-001" }
```

---

## 3. Client → Server Messages

### 3.1 Subscribe

Subscribe to real-time market data.

```json
{
  "action": "subscribe",
  "symbols": ["NFO:49543", "NFO:49545"],
  "request_id": "sub-001"
}
```

**Parameters:**
| Field | Type | Description |
|-------|------|-------------|
| `symbols` | string[] | Array of canonical symbols (`{exchange}:{exchange_token}`) |
| `request_id` | string? | Optional correlation ID |

### 3.2 Unsubscribe

Unsubscribe from symbols.

```json
{
  "action": "unsubscribe",
  "symbols": ["NFO:49543"],
  "request_id": "unsub-001"
}
```

### 3.3 Subscribe Strategy

Subscribe to strategy P&L updates.

```json
{
  "action": "subscribe_strategy",
  "strategy_id": "strat-001",
  "underlying": "NIFTY",
  "legs": [
    {"type": "CE", "side": "BUY", "strike": 26300, "qty": 1, "lot": 25, "premium": 250},
    {"type": "CE", "side": "SELL", "strike": 26500, "qty": 1, "lot": 25, "premium": 150}
  ],
  "request_id": "strat-sub-001"
}
```

### 3.4 Unsubscribe Strategy

```json
{
  "action": "unsubscribe_strategy",
  "strategy_id": "strat-001",
  "request_id": "strat-unsub-001"
}
```

### 3.5 Ping

Heartbeat message to keep connection alive.

```json
{
  "action": "ping"
}
```

### 3.6 Get Subscriptions

List current subscriptions.

```json
{
  "action": "get_subscriptions",
  "request_id": "list-001"
}
```

---

## 4. Server → Client Messages

### 4.1 Subscribed

Confirmation of subscription.

```json
{
  "type": "subscribed",
  "symbols": ["NFO:49543", "NFO:49545"],
  "request_id": "sub-001"
}
```

### 4.2 Unsubscribed

Confirmation of unsubscription.

```json
{
  "type": "unsubscribed",
  "symbols": ["NFO:49543"],
  "request_id": "unsub-001"
}
```

### 4.3 Depth Snapshot

Real-time market depth update.

```json
{
  "type": "depth_snapshot",
  "symbol": "NFO:49543",
  "instrument_id": 49543,
  "capture_time_ms": 1767521472123,
  "bids": [
    {"price": 284.00, "size": 225, "orders": 1},
    {"price": 283.50, "size": 450, "orders": 2}
  ],
  "asks": [
    {"price": 286.00, "size": 975, "orders": 3},
    {"price": 286.50, "size": 300, "orders": 1}
  ],
  "trade": {
    "last_price": 285.50,
    "last_qty": 50,
    "total_traded_quantity": 5250000,
    "ohlc": {"open": 275.00, "high": 290.00, "low": 272.00, "close": 280.00}
  },
  "source": "kite_ws",
  "is_partial": false,
  "is_stale": false
}
```

### 4.4 Feature Snapshot

Market microstructure features.

```json
{
  "type": "feature_snapshot",
  "symbol": "NFO:49543",
  "instrument_id": 49543,
  "timestamp": 1767521472123,
  "midprice": 285.00,
  "spread": 2.00,
  "spread_bps": 70.2,
  "bid_ask_imbalance": 0.35,
  "microprice": 284.50,
  "bid_depth": 3500,
  "ask_depth": 2800,
  "depth_slope": 0.12,
  "lpi": 0.85,
  "ofi": 125.5,
  "shock": 0.0,
  "is_stale": false,
  "is_partial": false
}
```

### 4.5 Execution Hint

Trading posture recommendation.

```json
{
  "type": "execution_hint",
  "symbol": "NFO:49543",
  "instrument_id": 49543,
  "capture_time_ms": 1767521472123,
  "posture": "PASSIVE",
  "reasons": [
    "TIGHT_SPREAD: Spread 70 bps within threshold",
    "FAVORABLE_IMBALANCE: Bid-heavy order book"
  ],
  "confidence": 0.72,
  "spread_bps": 70.2,
  "imbalance": 0.35,
  "depth_slope": 0.12,
  "shock": 0.0
}
```

### 4.6 Strategy Update

Strategy P&L update.

```json
{
  "type": "strategy_update",
  "strategy_id": "strat-001",
  "timestamp": 1767521472123,
  "underlying_price": 26325,
  "total_pnl": 1250.00,
  "total_pnl_pct": 5.2,
  "legs": [
    {
      "type": "CE",
      "strike": 26300,
      "current_price": 265.00,
      "entry_price": 250.00,
      "pnl": 375.00,
      "iv": 0.148,
      "delta": 0.54,
      "theta": -14.50
    },
    {
      "type": "CE",
      "strike": 26500,
      "current_price": 165.00,
      "entry_price": 150.00,
      "pnl": -375.00,
      "iv": 0.152,
      "delta": 0.38,
      "theta": -12.00
    }
  ],
  "greeks": {
    "delta": 0.16,
    "gamma": 0.0003,
    "theta": -2.50,
    "vega": 12.00
  }
}
```

### 4.7 Quote

Simple quote update (without full depth).

```json
{
  "type": "quote",
  "symbol": "NFO:49543",
  "last_price": 285.50,
  "change": 5.50,
  "change_pct": 1.96,
  "bid": 284.00,
  "ask": 286.00,
  "volume": 5250000,
  "oi": 12500000,
  "timestamp": 1767521472123
}
```

### 4.8 Pong

Response to ping.

```json
{
  "type": "pong",
  "server_time": 1767521472123
}
```

### 4.9 Subscriptions List

Current subscriptions.

```json
{
  "type": "subscriptions",
  "symbols": ["NFO:49543", "NFO:49545"],
  "strategies": ["strat-001"],
  "request_id": "list-001"
}
```

### 4.10 Error

Error message.

```json
{
  "type": "error",
  "code": "INVALID_SYMBOL",
  "message": "Symbol NFO:99999 not found",
  "request_id": "sub-001"
}
```

**Error Codes:**
| Code | Description |
|------|-------------|
| `INVALID_SYMBOL` | Symbol format invalid or not found |
| `SUBSCRIPTION_LIMIT` | Too many subscriptions |
| `INVALID_MESSAGE` | Malformed message |
| `RATE_LIMITED` | Too many requests |
| `INTERNAL_ERROR` | Server error |

---

## 5. TypeScript Client

### 5.1 WebSocket Client Class

```typescript
// lib/ws-client.ts

export type MessageHandler = (message: any) => void;
export type ConnectionHandler = () => void;
export type ErrorHandler = (error: Event) => void;

interface WSClientOptions {
  url: string;
  reconnectInterval?: number;
  maxReconnectAttempts?: number;
  heartbeatInterval?: number;
}

export class PayoffWSClient {
  private ws: WebSocket | null = null;
  private url: string;
  private reconnectInterval: number;
  private maxReconnectAttempts: number;
  private heartbeatInterval: number;
  private reconnectAttempts = 0;
  private heartbeatTimer: NodeJS.Timer | null = null;
  private messageHandlers = new Map<string, Set<MessageHandler>>();
  private onConnect: ConnectionHandler | null = null;
  private onDisconnect: ConnectionHandler | null = null;
  private onError: ErrorHandler | null = null;
  private pendingRequests = new Map<string, (response: any) => void>();

  constructor(options: WSClientOptions) {
    this.url = options.url;
    this.reconnectInterval = options.reconnectInterval || 5000;
    this.maxReconnectAttempts = options.maxReconnectAttempts || 10;
    this.heartbeatInterval = options.heartbeatInterval || 30000;
  }

  connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      try {
        this.ws = new WebSocket(this.url);

        this.ws.onopen = () => {
          this.reconnectAttempts = 0;
          this.startHeartbeat();
          this.onConnect?.();
          resolve();
        };

        this.ws.onmessage = (event) => {
          this.handleMessage(JSON.parse(event.data));
        };

        this.ws.onclose = () => {
          this.stopHeartbeat();
          this.onDisconnect?.();
          this.attemptReconnect();
        };

        this.ws.onerror = (error) => {
          this.onError?.(error);
          reject(error);
        };
      } catch (error) {
        reject(error);
      }
    });
  }

  disconnect(): void {
    this.stopHeartbeat();
    this.ws?.close();
    this.ws = null;
  }

  // ========================================================================
  // Subscriptions
  // ========================================================================

  subscribe(symbols: string[]): Promise<void> {
    return this.sendRequest('subscribe', { symbols });
  }

  unsubscribe(symbols: string[]): Promise<void> {
    return this.sendRequest('unsubscribe', { symbols });
  }

  subscribeStrategy(
    strategyId: string,
    underlying: string,
    legs: any[]
  ): Promise<void> {
    return this.sendRequest('subscribe_strategy', {
      strategy_id: strategyId,
      underlying,
      legs,
    });
  }

  unsubscribeStrategy(strategyId: string): Promise<void> {
    return this.sendRequest('unsubscribe_strategy', {
      strategy_id: strategyId,
    });
  }

  getSubscriptions(): Promise<{ symbols: string[]; strategies: string[] }> {
    return this.sendRequest('get_subscriptions', {});
  }

  // ========================================================================
  // Event Handlers
  // ========================================================================

  onMessage(type: string, handler: MessageHandler): () => void {
    if (!this.messageHandlers.has(type)) {
      this.messageHandlers.set(type, new Set());
    }
    this.messageHandlers.get(type)!.add(handler);

    // Return unsubscribe function
    return () => {
      this.messageHandlers.get(type)?.delete(handler);
    };
  }

  setOnConnect(handler: ConnectionHandler): void {
    this.onConnect = handler;
  }

  setOnDisconnect(handler: ConnectionHandler): void {
    this.onDisconnect = handler;
  }

  setOnError(handler: ErrorHandler): void {
    this.onError = handler;
  }

  // ========================================================================
  // Internal
  // ========================================================================

  private sendRequest<T>(action: string, payload: any): Promise<T> {
    return new Promise((resolve, reject) => {
      if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
        reject(new Error('WebSocket not connected'));
        return;
      }

      const requestId = `${action}-${Date.now()}-${Math.random().toString(36).slice(2)}`;
      
      const message = {
        action,
        request_id: requestId,
        ...payload,
      };

      this.pendingRequests.set(requestId, resolve);
      
      setTimeout(() => {
        if (this.pendingRequests.has(requestId)) {
          this.pendingRequests.delete(requestId);
          reject(new Error('Request timeout'));
        }
      }, 10000);

      this.ws.send(JSON.stringify(message));
    });
  }

  private handleMessage(message: any): void {
    // Handle request-response correlation
    if (message.request_id && this.pendingRequests.has(message.request_id)) {
      const resolve = this.pendingRequests.get(message.request_id)!;
      this.pendingRequests.delete(message.request_id);
      resolve(message);
      return;
    }

    // Handle errors
    if (message.type === 'error') {
      console.error('WebSocket error:', message);
      return;
    }

    // Dispatch to type-specific handlers
    const handlers = this.messageHandlers.get(message.type);
    if (handlers) {
      handlers.forEach((handler) => handler(message));
    }

    // Also dispatch to wildcard handlers
    const wildcardHandlers = this.messageHandlers.get('*');
    if (wildcardHandlers) {
      wildcardHandlers.forEach((handler) => handler(message));
    }
  }

  private startHeartbeat(): void {
    this.heartbeatTimer = setInterval(() => {
      if (this.ws?.readyState === WebSocket.OPEN) {
        this.ws.send(JSON.stringify({ action: 'ping' }));
      }
    }, this.heartbeatInterval);
  }

  private stopHeartbeat(): void {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
  }

  private attemptReconnect(): void {
    if (this.reconnectAttempts >= this.maxReconnectAttempts) {
      console.error('Max reconnection attempts reached');
      return;
    }

    this.reconnectAttempts++;
    console.log(`Reconnecting in ${this.reconnectInterval}ms (attempt ${this.reconnectAttempts})`);

    setTimeout(() => {
      this.connect().catch((error) => {
        console.error('Reconnection failed:', error);
      });
    }, this.reconnectInterval);
  }
}

// Singleton instance
export const wsClient = new PayoffWSClient({
  url: process.env.NEXT_PUBLIC_WS_URL || 'ws://localhost:8080/ws',
});
```

---

## 6. React Hook

### 6.1 useWebSocket Hook

```typescript
// hooks/useWebSocket.ts
import { useEffect, useRef, useCallback, useState } from 'react';
import { wsClient } from '@/lib/ws-client';
import type { DepthSnapshot, FeatureSnapshot, ExecutionHint } from '@/types/payoff-engine';

interface UseWebSocketOptions {
  symbols?: string[];
  autoConnect?: boolean;
}

interface UseWebSocketResult {
  isConnected: boolean;
  error: Error | null;
  subscribe: (symbols: string[]) => Promise<void>;
  unsubscribe: (symbols: string[]) => Promise<void>;
  latestQuotes: Map<string, DepthSnapshot>;
  latestFeatures: Map<string, FeatureSnapshot>;
  latestHints: Map<string, ExecutionHint>;
}

export function useWebSocket(options: UseWebSocketOptions = {}): UseWebSocketResult {
  const { symbols = [], autoConnect = true } = options;
  
  const [isConnected, setIsConnected] = useState(false);
  const [error, setError] = useState<Error | null>(null);
  const [latestQuotes, setLatestQuotes] = useState<Map<string, DepthSnapshot>>(new Map());
  const [latestFeatures, setLatestFeatures] = useState<Map<string, FeatureSnapshot>>(new Map());
  const [latestHints, setLatestHints] = useState<Map<string, ExecutionHint>>(new Map());
  
  const unsubscribersRef = useRef<(() => void)[]>([]);

  // Connect on mount
  useEffect(() => {
    if (!autoConnect) return;

    wsClient.setOnConnect(() => setIsConnected(true));
    wsClient.setOnDisconnect(() => setIsConnected(false));
    wsClient.setOnError((e) => setError(new Error('WebSocket error')));

    wsClient.connect().catch((e) => setError(e));

    return () => {
      wsClient.disconnect();
    };
  }, [autoConnect]);

  // Set up message handlers
  useEffect(() => {
    const unsubDepth = wsClient.onMessage('depth_snapshot', (msg: DepthSnapshot) => {
      setLatestQuotes((prev) => new Map(prev).set(msg.symbol, msg));
    });

    const unsubFeatures = wsClient.onMessage('feature_snapshot', (msg: FeatureSnapshot) => {
      setLatestFeatures((prev) => new Map(prev).set(msg.symbol, msg));
    });

    const unsubHints = wsClient.onMessage('execution_hint', (msg: ExecutionHint) => {
      setLatestHints((prev) => new Map(prev).set(msg.symbol, msg));
    });

    unsubscribersRef.current = [unsubDepth, unsubFeatures, unsubHints];

    return () => {
      unsubscribersRef.current.forEach((unsub) => unsub());
    };
  }, []);

  // Subscribe to initial symbols
  useEffect(() => {
    if (isConnected && symbols.length > 0) {
      wsClient.subscribe(symbols).catch(console.error);
    }

    return () => {
      if (isConnected && symbols.length > 0) {
        wsClient.unsubscribe(symbols).catch(console.error);
      }
    };
  }, [isConnected, symbols]);

  const subscribe = useCallback(async (newSymbols: string[]) => {
    if (isConnected) {
      await wsClient.subscribe(newSymbols);
    }
  }, [isConnected]);

  const unsubscribe = useCallback(async (removeSymbols: string[]) => {
    if (isConnected) {
      await wsClient.unsubscribe(removeSymbols);
    }
  }, [isConnected]);

  return {
    isConnected,
    error,
    subscribe,
    unsubscribe,
    latestQuotes,
    latestFeatures,
    latestHints,
  };
}
```

---

## 7. Error Handling

### 7.1 Connection Error Recovery

```typescript
function ConnectionStatus() {
  const { isConnected, error } = useWebSocket({ autoConnect: true });

  if (error) {
    return (
      <div className="flex items-center gap-2 text-red-500">
        <span className="w-2 h-2 bg-red-500 rounded-full" />
        Connection Error: {error.message}
      </div>
    );
  }

  return (
    <div className={`flex items-center gap-2 ${isConnected ? 'text-green-500' : 'text-yellow-500'}`}>
      <span className={`w-2 h-2 rounded-full ${isConnected ? 'bg-green-500' : 'bg-yellow-500 animate-pulse'}`} />
      {isConnected ? 'Connected' : 'Connecting...'}
    </div>
  );
}
```

### 7.2 Stale Data Handling

```typescript
function QuoteDisplay({ symbol }: { symbol: string }) {
  const { latestQuotes } = useWebSocket({ symbols: [symbol] });
  const quote = latestQuotes.get(symbol);

  if (!quote) return <Skeleton className="h-8 w-24" />;

  return (
    <div className={`flex items-center gap-2 ${quote.is_stale ? 'opacity-50' : ''}`}>
      <span className="font-mono text-lg">
        ₹{quote.trade.last_price.toFixed(2)}
      </span>
      {quote.is_stale && (
        <span className="text-xs text-yellow-500">Stale</span>
      )}
    </div>
  );
}
```

---

## 📊 Message Flow Diagram

```
┌─────────────────┐                              ┌─────────────────┐
│    FRONTEND     │                              │    BACKEND      │
└────────┬────────┘                              └────────┬────────┘
         │                                                │
         │  ══════════ WebSocket Connect ═══════════════> │
         │                                                │
         │  <═══════════ Connected ═══════════════════════│
         │                                                │
         │  {"action":"subscribe",                        │
         │   "symbols":["NFO:49543"]} ─────────────────> │
         │                                                │
         │  <──────── {"type":"subscribed"} ─────────────│
         │                                                │
         │  <──────── {"type":"depth_snapshot",...} ─────│
         │  <──────── {"type":"feature_snapshot",...} ───│
         │  <──────── {"type":"execution_hint",...} ─────│
         │                                                │
         │  {"action":"ping"} ─────────────────────────> │
         │  <──────── {"type":"pong"} ───────────────────│
         │                                                │
```

---

## 🔗 Related Documents

- [DATA_SCHEMA.md](./DATA_SCHEMA.md) - Message type definitions
- [API_REFERENCE.md](./API_REFERENCE.md) - REST API endpoints
- [FRONTEND_INTEGRATION.md](./FRONTEND_INTEGRATION.md) - Integration guide
