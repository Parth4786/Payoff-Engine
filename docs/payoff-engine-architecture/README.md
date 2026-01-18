# Payoff Engine - Architecture Documentation

> Complete architecture blueprint for the Payoff Engine C++ backend, designed for frontend integration.

## 📚 Documentation Index

| Document | Description | Audience |
|----------|-------------|----------|
| [OVERVIEW.md](./OVERVIEW.md) | High-level system architecture, module overview, design principles | All developers |
| [DATA_SCHEMA.md](./DATA_SCHEMA.md) | Complete data type definitions, JSON contracts, TypeScript interfaces | Frontend developers |
| [API_REFERENCE.md](./API_REFERENCE.md) | REST API endpoint specifications, request/response examples | Frontend developers |
| [DATA_FLOW.md](./DATA_FLOW.md) | Detailed data pipeline flows, transformation chains | Backend & Frontend |
| [FRONTEND_INTEGRATION.md](./FRONTEND_INTEGRATION.md) | Practical integration guide, React hooks, state management | Frontend developers |
| [WEBSOCKET_PROTOCOL.md](./WEBSOCKET_PROTOCOL.md) | Real-time streaming protocol, TypeScript client | Frontend developers |

## 🚀 Quick Start for Frontend Developers

### 1. Understand the Architecture
Start with [OVERVIEW.md](./OVERVIEW.md) to understand:
- System layers and data flow
- Design principle: **Frontend is render-only**
- Module responsibilities

### 2. Learn the Data Types
Read [DATA_SCHEMA.md](./DATA_SCHEMA.md) to understand:
- Core types: `DepthSnapshot`, `OptionLeg`, `Strategy`
- Payoff types: `PayoffCurve`, `Greeks`, `PayoffPoint`
- Screener types: `InstrumentSnapshot`, `MarketScreenerResult`
- TypeScript interfaces for all types

### 3. Reference API Endpoints
Use [API_REFERENCE.md](./API_REFERENCE.md) for:
- REST endpoint specifications
- Query parameters and request bodies
- Response format examples
- Error codes

### 4. Implement Integration
Follow [FRONTEND_INTEGRATION.md](./FRONTEND_INTEGRATION.md) for:
- API client setup
- React component patterns
- State management (Zustand/React Query)
- Error handling patterns
- Performance optimization

### 5. Add Real-time Updates
Implement [WEBSOCKET_PROTOCOL.md](./WEBSOCKET_PROTOCOL.md) for:
- WebSocket connection management
- Message protocol
- TypeScript client class
- React hooks for streaming data

## 🏗️ Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                       FRONTEND                              │
│  React/Vue/Svelte • Render-only • No business logic         │
└─────────────────────────┬───────────────────────────────────┘
                          │ REST + WebSocket
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                       API LAYER                             │
│  REST Server • WebSocket Server • Screener Routes           │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                    COMPUTATION LAYER                        │
│  Payoff Engine • Pricing Engine • Feature Engine • Screener │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                       DATA LAYER                            │
│  ClickHouse • Kite WS • Instrument Manager • Streaming      │
└─────────────────────────────────────────────────────────────┘
```

## 📋 Key Contracts

### Canonical Symbol Format
```
{exchange}:{exchange_token}
Example: NFO:49543
```

### Data Quality Flags
```json
{
  "source": "clickhouse | kite_ws | mock",
  "is_partial": false,
  "is_stale": false
}
```

### API Response Pattern
```json
{
  "status": "ok",
  "data": { ... },
  "timestamp": 1767521472123
}
```

## 🔑 Critical Rules

1. **Never calculate Greeks client-side** - Use `/api/greeks/calculate`
2. **Always use `exchange_token` as `instrument_id`** - Never `instrument_token`
3. **Handle `is_stale` flag** - Show warnings to users
4. **Validate all data before rendering** - Check for null/undefined
5. **Don't hardcode lot sizes** - Fetch from instrument info

## 📁 Related Documentation

- [../ARCHITECTURE.md](../ARCHITECTURE.md) - Original C++ architecture proposal
- [../API_DTO_SCHEMA.md](../API_DTO_SCHEMA.md) - Original DTO schema
- [../DATA_CONTRACT.md](../DATA_CONTRACT.md) - Data contract specification
- [../payoff_feature_spec.md](../payoff_feature_spec.md) - Feature requirements

---

*Last updated: January 2026*
