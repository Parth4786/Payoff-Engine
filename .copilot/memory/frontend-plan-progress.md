# Frontend Architecture Plan Progress

## Status: IN PROGRESS
Last updated: Session ongoing

## Completed
- ✅ Tech stack selected: Next.js 14, React 18, TypeScript, Zustand, TanStack Query, Tailwind, shadcn/ui
- ✅ Backend capability analysis (17+ endpoints mapped)
- ✅ 9 ASCII wireframes created (Strategy Builder, Payoff, Sensitivity, Screener, Option Chain, IV Surface, Risk, Live Monitor, **REPLAY MODE**)
- ✅ Component architecture with folder structure
- ✅ TypeScript interfaces for all DTOs + Replay types
- ✅ Design guidelines (dark theme, color palette)
- ✅ **NEW BACKEND ENDPOINTS DEFINED** for Replay Mode

## Primary Focus
**REPLAY MODE** - User's main use case:
- Go back in time (timestamp T)
- See what payoff/Greeks looked like at T (NO future data)
- Then see actual outcome at T+N
- Compare prediction vs reality to find insights

## New Backend Endpoints Needed

### 1. POST /api/replay/strategy
- Run strategy through historical data
- Get payoff snapshot at each timestamp
- Returns: `snapshots[]`, `summary`

### 2. POST /api/replay/prediction
- Get predicted payoff AS OF timestamp T
- Uses only data available at T (no lookahead)
- Returns: predictions at multiple horizons

### 3. POST /api/replay/compare
- Compare prediction at T vs actual at T+N
- Calculates deviation metrics
- Returns: deviation analysis, insights[]

### 4. Session Management
- POST /api/replay/session/create
- GET /api/replay/session/{id}/state
- POST /api/replay/session/{id}/step
- POST /api/replay/session/{id}/seek
- DELETE /api/replay/session/{id}

### 5. GET /api/replay/events
- Market events in time range (IV spike, price gap, OI buildup)
- For annotation on timeline

### 6. POST /api/payoff/historical-batch
- Calculate payoff at multiple timestamps efficiently

## Plan File Location
`c:\Users\LENOVO\Desktop\Payoff-Engine\.github\prompts\plan-frontendArchitecture.prompt.md`

## Next Steps
1. User to review backend endpoints needed
2. Implement backend endpoints in C++
3. Begin frontend implementation starting with Replay Mode
