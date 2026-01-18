#pragma once
/**
 * @file websocket_server.hpp
 * @brief WebSocket server interface for frontend clients
 */

#include "core/models.hpp"
#include <cstdint>
#include <string>

namespace payoff::api {

/**
 * @brief Start the WebSocket server on specified port
 * @param port Port number (default 8081)
 */
void start_websocket_server(int port = 8081);

/**
 * @brief Stop the WebSocket server
 */
void stop_websocket_server();

/**
 * @brief Broadcast a tick to all connected clients
 */
void broadcast_tick(uint32_t token, double ltp, int64_t volume);

/**
 * @brief Broadcast Greeks calculation to all clients
 */
void broadcast_greeks(double spot, double strike, const std::string& type,
                      double delta, double gamma, double theta, double vega);

/**
 * @brief Broadcast a full depth snapshot to clients
 */
void broadcast_depth_snapshot(const core::DepthSnapshot& snapshot);

/**
 * @brief Get number of connected clients
 */
size_t websocket_client_count();

} // namespace payoff::api
