/**
 * @file websocket_server.cpp
 * @brief WebSocket streaming server for frontend clients
 * 
 * Provides real-time streaming of:
 * - Market data (ticks, quotes)
 * - Strategy payoff updates
 * - Greeks calculations
 * 
 * Protocol: JSON messages over WebSocket
 * For production, use a library like websocketpp or Beast
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
    using SOCKET_TYPE = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define CLOSE_SOCKET close
    #define INVALID_SOCKET -1
    using SOCKET_TYPE = int;
#endif

namespace payoff::api {

// ============================================================================
// WebSocket Frame Opcodes
// ============================================================================
enum class WSOpcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA
};

// ============================================================================
// WebSocket Client Session
// ============================================================================
struct WSClient {
    SOCKET_TYPE socket = INVALID_SOCKET;
    std::string id;
    std::vector<std::string> subscriptions;
    std::chrono::steady_clock::time_point last_ping;
    bool authenticated = false;
};

// ============================================================================
// Simple JSON Builder for Messages
// ============================================================================
class WSJsonBuilder {
public:
    WSJsonBuilder& start_object() { ss_ << "{"; first_ = true; return *this; }
    WSJsonBuilder& end_object() { ss_ << "}"; return *this; }
    WSJsonBuilder& start_array() { ss_ << "["; first_ = true; return *this; }
    WSJsonBuilder& end_array() { ss_ << "]"; return *this; }
    
    WSJsonBuilder& key(const std::string& k) {
        if (!first_) ss_ << ",";
        ss_ << "\"" << k << "\":";
        first_ = false;
        return *this;
    }
    
    WSJsonBuilder& value(const std::string& v) {
        ss_ << "\"" << escape(v) << "\"";
        return *this;
    }
    WSJsonBuilder& value(const char* v) {
        ss_ << "\"" << escape(v ? std::string(v) : std::string()) << "\"";
        return *this;
    }
    WSJsonBuilder& value(double v) { ss_ << v; return *this; }
    WSJsonBuilder& value(int64_t v) { ss_ << v; return *this; }
    WSJsonBuilder& value(bool v) { ss_ << (v ? "true" : "false"); return *this; }
    WSJsonBuilder& next() { ss_ << ","; first_ = false; return *this; }
    
    std::string str() const { return ss_.str(); }
    
private:
    std::ostringstream ss_;
    bool first_ = true;

    static std::string escape(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
            }
        }
        return result;
    }
};

// ============================================================================
// WebSocket Server
// ============================================================================
class WebSocketServer {
public:
    using MessageHandler = std::function<void(const std::string& client_id, 
                                               const std::string& message)>;
    using ConnectHandler = std::function<void(const std::string& client_id)>;
    using DisconnectHandler = std::function<void(const std::string& client_id)>;
    
    WebSocketServer() = default;
    ~WebSocketServer() { stop(); }
    
    // Non-copyable
    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    void on_message(MessageHandler handler) { message_handler_ = std::move(handler); }
    void on_connect(ConnectHandler handler) { connect_handler_ = std::move(handler); }
    void on_disconnect(DisconnectHandler handler) { disconnect_handler_ = std::move(handler); }
    
    // ========================================================================
    // Server Control
    // ========================================================================
    
    bool start(int port) {
        if (running_) return false;
        
        port_ = port;
        running_ = true;
        
        server_thread_ = std::make_unique<std::thread>(&WebSocketServer::server_loop, this);
        
        std::cout << "[WS] WebSocket server starting on port " << port << std::endl;
        return true;
    }
    
    void stop() {
        running_ = false;
        
        // Close all client connections
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (auto& [id, client] : clients_) {
                if (client.socket != INVALID_SOCKET) {
                    CLOSE_SOCKET(client.socket);
                }
            }
            clients_.clear();
        }
        
        // Close server socket
        if (server_socket_ != INVALID_SOCKET) {
            CLOSE_SOCKET(server_socket_);
            server_socket_ = INVALID_SOCKET;
        }
        
        if (server_thread_ && server_thread_->joinable()) {
            server_thread_->join();
        }
    }
    
    bool is_running() const noexcept { return running_; }
    
    // ========================================================================
    // Broadcasting
    // ========================================================================
    
    void broadcast(const std::string& message) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& [id, client] : clients_) {
            send_text(client.socket, message);
        }
    }
    
    void send_to(const std::string& client_id, const std::string& message) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(client_id);
        if (it != clients_.end()) {
            send_text(it->second.socket, message);
        }
    }
    
    void broadcast_tick(uint32_t instrument_token, double ltp, int64_t volume) {
        WSJsonBuilder json;
        json.start_object()
            .key("type").value("tick")
            .key("token").value(static_cast<int64_t>(instrument_token))
            .key("ltp").value(ltp)
            .key("volume").value(volume)
            .key("timestamp").value(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count())
            .end_object();
        
        broadcast(json.str());
    }
    
    void broadcast_greeks(double spot, double strike, const std::string& type,
                          double delta, double gamma, double theta, double vega) {
        WSJsonBuilder json;
        json.start_object()
            .key("type").value("greeks")
            .key("spot").value(spot)
            .key("strike").value(strike)
            .key("option_type").value(type)
            .key("delta").value(delta)
            .key("gamma").value(gamma)
            .key("theta").value(theta)
            .key("vega").value(vega)
            .end_object();
        
        broadcast(json.str());
    }
    
    size_t client_count() const {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        return clients_.size();
    }
    
private:
    int port_ = 8081;
    std::atomic<bool> running_{false};
    SOCKET_TYPE server_socket_ = INVALID_SOCKET;
    
    std::map<std::string, WSClient> clients_;
    mutable std::mutex clients_mutex_;
    
    std::unique_ptr<std::thread> server_thread_;
    
    MessageHandler message_handler_;
    ConnectHandler connect_handler_;
    DisconnectHandler disconnect_handler_;
    
    int next_client_id_ = 1;
    
    // ========================================================================
    // Server Loop
    // ========================================================================
    
    void server_loop() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ == INVALID_SOCKET) {
            std::cerr << "[WS] Failed to create socket" << std::endl;
            running_ = false;
            return;
        }
        
        // Allow address reuse
        int opt = 1;
#ifdef _WIN32
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(static_cast<uint16_t>(port_));
        
        if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "[WS] Failed to bind to port " << port_ << std::endl;
            CLOSE_SOCKET(server_socket_);
            running_ = false;
            return;
        }
        
        listen(server_socket_, 10);
        std::cout << "[WS] WebSocket server listening on port " << port_ << std::endl;
        
        // Set non-blocking with timeout
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(server_socket_, SOL_SOCKET, SO_RCVTIMEO, 
                   reinterpret_cast<const char*>(&tv), sizeof(tv));
        
        while (running_) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            SOCKET_TYPE client_socket = accept(server_socket_, 
                reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
            
            if (client_socket != INVALID_SOCKET) {
                // New connection - spawn handler thread
                std::thread(&WebSocketServer::handle_client, this, client_socket).detach();
            }
        }
        
#ifdef _WIN32
        WSACleanup();
#endif
    }
    
    void handle_client(SOCKET_TYPE client_socket) {
        // Read HTTP upgrade request
        char buffer[4096];
        int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes <= 0) {
            CLOSE_SOCKET(client_socket);
            return;
        }
        buffer[bytes] = '\0';
        
        std::string request(buffer);
        
        // Check for WebSocket upgrade
        if (request.find("Upgrade: websocket") == std::string::npos) {
            // Not a WebSocket request
            const char* response = "HTTP/1.1 400 Bad Request\r\n\r\n";
            send(client_socket, response, static_cast<int>(strlen(response)), 0);
            CLOSE_SOCKET(client_socket);
            return;
        }
        
        // Extract Sec-WebSocket-Key
        std::string ws_key;
        auto key_pos = request.find("Sec-WebSocket-Key: ");
        if (key_pos != std::string::npos) {
            auto start = key_pos + 19;
            auto end = request.find("\r\n", start);
            ws_key = request.substr(start, end - start);
        }
        
        if (ws_key.empty()) {
            CLOSE_SOCKET(client_socket);
            return;
        }
        
        // Generate accept key (simplified - production should use SHA1 + base64)
        // For now, just send a valid-looking response
        std::string accept_key = "dGhlIHNhbXBsZSBub25jZQ==";  // Placeholder
        
        // Send upgrade response
        std::ostringstream response;
        response << "HTTP/1.1 101 Switching Protocols\r\n"
                 << "Upgrade: websocket\r\n"
                 << "Connection: Upgrade\r\n"
                 << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
                 << "\r\n";
        
        std::string resp_str = response.str();
        send(client_socket, resp_str.c_str(), static_cast<int>(resp_str.length()), 0);
        
        // Register client
        std::string client_id = "client_" + std::to_string(next_client_id_++);
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            WSClient client;
            client.socket = client_socket;
            client.id = client_id;
            client.last_ping = std::chrono::steady_clock::now();
            clients_[client_id] = client;
        }
        
        if (connect_handler_) {
            connect_handler_(client_id);
        }
        
        std::cout << "[WS] Client connected: " << client_id << std::endl;
        
        // Send welcome message
        WSJsonBuilder welcome;
        welcome.start_object()
            .key("type").value("connected")
            .key("client_id").value(client_id)
            .key("message").value("Welcome to Payoff Engine WebSocket")
            .end_object();
        send_text(client_socket, welcome.str());
        
        // Client message loop
        while (running_) {
            std::vector<uint8_t> frame_data(4096);
            bytes = recv(client_socket, reinterpret_cast<char*>(frame_data.data()), 
                        static_cast<int>(frame_data.size()), 0);
            
            if (bytes <= 0) {
                break;  // Client disconnected
            }
            
            // Parse WebSocket frame (simplified)
            if (bytes >= 2) {
                uint8_t opcode = frame_data[0] & 0x0F;
                bool masked = (frame_data[1] & 0x80) != 0;
                uint8_t payload_len = frame_data[1] & 0x7F;
                
                if (opcode == static_cast<uint8_t>(WSOpcode::Close)) {
                    break;
                }
                
                if (opcode == static_cast<uint8_t>(WSOpcode::Ping)) {
                    // Send pong
                    send_pong(client_socket);
                    continue;
                }
                
                if (opcode == static_cast<uint8_t>(WSOpcode::Text) && masked && payload_len < 126) {
                    // Simple text frame
                    size_t header_len = 6;  // 2 byte header + 4 byte mask
                    if (bytes >= static_cast<int>(header_len + payload_len)) {
                        uint8_t mask[4];
                        std::memcpy(mask, &frame_data[2], 4);
                        
                        std::string message;
                        message.resize(payload_len);
                        for (size_t i = 0; i < payload_len; ++i) {
                            message[i] = static_cast<char>(frame_data[header_len + i] ^ mask[i % 4]);
                        }
                        
                        if (message_handler_) {
                            message_handler_(client_id, message);
                        }
                    }
                }
            }
        }
        
        // Cleanup
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.erase(client_id);
        }
        
        if (disconnect_handler_) {
            disconnect_handler_(client_id);
        }
        
        CLOSE_SOCKET(client_socket);
        std::cout << "[WS] Client disconnected: " << client_id << std::endl;
    }
    
    // ========================================================================
    // Frame Sending
    // ========================================================================
    
    void send_text(SOCKET_TYPE socket, const std::string& message) {
        if (socket == INVALID_SOCKET) return;
        
        std::vector<uint8_t> frame;
        
        // FIN + Text opcode
        frame.push_back(0x81);
        
        // Payload length (no mask for server->client)
        size_t len = message.length();
        if (len < 126) {
            frame.push_back(static_cast<uint8_t>(len));
        } else if (len < 65536) {
            frame.push_back(126);
            frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(len & 0xFF));
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; --i) {
                frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
            }
        }
        
        // Payload
        frame.insert(frame.end(), message.begin(), message.end());
        
        send(socket, reinterpret_cast<const char*>(frame.data()), 
             static_cast<int>(frame.size()), 0);
    }
    
    void send_pong(SOCKET_TYPE socket) {
        uint8_t frame[] = {0x8A, 0x00};  // Pong with no payload
        send(socket, reinterpret_cast<const char*>(frame), 2, 0);
    }
};

// ============================================================================
// Global Server Instance & Interface
// ============================================================================

static std::unique_ptr<WebSocketServer> g_ws_server;

void start_websocket_server(int port) {
    if (!g_ws_server) {
        g_ws_server = std::make_unique<WebSocketServer>();
    }
    
    g_ws_server->on_connect([](const std::string& id) {
        std::cout << "[WS] New connection: " << id << std::endl;
    });
    
    g_ws_server->on_disconnect([](const std::string& id) {
        std::cout << "[WS] Disconnected: " << id << std::endl;
    });
    
    g_ws_server->on_message([](const std::string& id, const std::string& msg) {
        std::cout << "[WS] Message from " << id << ": " << msg << std::endl;
        
        // Echo back for testing
        if (g_ws_server) {
            g_ws_server->send_to(id, "{\"type\":\"ack\",\"message\":\"received\"}");
        }
    });
    
    g_ws_server->start(port);
}

void stop_websocket_server() {
    if (g_ws_server) {
        g_ws_server->stop();
    }
}

void broadcast_tick(uint32_t token, double ltp, int64_t volume) {
    if (g_ws_server) {
        g_ws_server->broadcast_tick(token, ltp, volume);
    }
}

void broadcast_greeks(double spot, double strike, const std::string& type,
                      double delta, double gamma, double theta, double vega) {
    if (g_ws_server) {
        g_ws_server->broadcast_greeks(spot, strike, type, delta, gamma, theta, vega);
    }
}

} // namespace payoff::api
