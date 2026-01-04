# Payoff Engine - C++ Backend Implementation Progress

## Current Task
Implement C++ backend features with test scripts (NO PYTHON - C++ only):
- API Layer with cpp-httplib REST endpoints
- WebSocket for live streaming
- ClickHouse data source (complete implementation)
- Kite API integration
- Test suite for all features

## Test Instrument
- Symbol: NIFTY Jan FUT
- Price: 26300
- Multi-client support required

## Environment (.env)
- ClickHouse: 110.172.21.62:8123, tick_data_db
- Kite: api_key=za4r7gn8aqq3tnwb, user=LEY228

## Existing C++ Structure
- cpp/include/: Headers for core, payoff, execution, features, streaming, resampling, cache
- cpp/src/: Implementation files
- cpp/tests/: test_models, test_features, test_payoff, test_streaming, test_parity

## TODO
1. [ ] Implement cpp-httplib REST server
2. [ ] Implement WebSocket server for live streaming  
3. [ ] Complete ClickHouse source (HTTP API)
4. [ ] Add Kite API client (REST)
5. [ ] Add Kite WebSocket client
6. [ ] Create comprehensive test executables
7. [ ] Test all features with NIFTY Jan FUT @ 26300

## Files to Create/Update
- src/api/rest_server.cpp - Full implementation
- src/api/websocket_server.cpp - Full implementation
- src/datasource/clickhouse_source.cpp - Full implementation
- src/kite/kite_client.cpp - NEW
- src/kite/kite_websocket.cpp - NEW
- include/kite/ - Headers
- tests/test_integration.cpp - Integration tests
