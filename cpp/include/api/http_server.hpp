#pragma once
/**
 * @file http_server.hpp
 * @brief Lightweight HTTP server for REST API
 * 
 * Header-only, minimal implementation inspired by cpp-httplib.
 * For production, consider using actual cpp-httplib or Boost.Beast.
 */

#include <algorithm>
#include <chrono>
#include <functional>
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

namespace payoff::http {

// ============================================================================
// Request / Response
// ============================================================================

struct Request {
    std::string method;
    std::string path;
    std::string query_string;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> params;  // Query params
    std::string body;
    
    [[nodiscard]] std::string get_param(const std::string& key, 
                                         const std::string& default_val = "") const {
        auto it = params.find(key);
        return (it != params.end()) ? it->second : default_val;
    }
    
    [[nodiscard]] int get_param_int(const std::string& key, int default_val = 0) const {
        auto it = params.find(key);
        if (it == params.end()) return default_val;
        try {
            return std::stoi(it->second);
        } catch (...) {
            return default_val;
        }
    }
    
    [[nodiscard]] double get_param_double(const std::string& key, double default_val = 0.0) const {
        auto it = params.find(key);
        if (it == params.end()) return default_val;
        try {
            return std::stod(it->second);
        } catch (...) {
            return default_val;
        }
    }
};

struct Response {
    int status = 200;
    std::map<std::string, std::string> headers;
    std::string body;
    
    void set_content(const std::string& content, const std::string& content_type) {
        body = content;
        headers["Content-Type"] = content_type;
    }
    
    void set_json(const std::string& json) {
        set_content(json, "application/json");
    }
    
    void set_header(const std::string& key, const std::string& value) {
        headers[key] = value;
    }
};

// ============================================================================
// Route Handler
// ============================================================================

using Handler = std::function<void(const Request&, Response&)>;

struct Route {
    std::string method;
    std::string pattern;  // Simple pattern, e.g., "/api/payoff"
    Handler handler;
};

// ============================================================================
// HTTP Server
// ============================================================================

class Server {
public:
    Server() = default;
    ~Server() {
        stop();
    }
    
    // Non-copyable
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    
    // ========================================================================
    // Route Registration
    // ========================================================================
    
    void Get(const std::string& pattern, Handler handler) {
        routes_.push_back({"GET", pattern, std::move(handler)});
    }
    
    void Post(const std::string& pattern, Handler handler) {
        routes_.push_back({"POST", pattern, std::move(handler)});
    }
    
    void Put(const std::string& pattern, Handler handler) {
        routes_.push_back({"PUT", pattern, std::move(handler)});
    }
    
    void Delete(const std::string& pattern, Handler handler) {
        routes_.push_back({"DELETE", pattern, std::move(handler)});
    }
    
    void Options(const std::string& pattern, Handler handler) {
        routes_.push_back({"OPTIONS", pattern, std::move(handler)});
    }
    
    // ========================================================================
    // CORS
    // ========================================================================
    
    void enable_cors(const std::string& allowed_origins = "*") {
        cors_enabled_ = true;
        cors_origins_ = allowed_origins;
    }
    
    // ========================================================================
    // Server Control
    // ========================================================================
    
    bool listen(const std::string& host, int port) {
        host_ = host;
        port_ = port;
        
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return false;
        }
#endif
        
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ == INVALID_SOCKET) {
            return false;
        }
        
        // Allow socket reuse
        int opt = 1;
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<const char*>(&opt), sizeof(opt));
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        
        if (host == "0.0.0.0" || host.empty()) {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            addr.sin_addr.s_addr = inet_addr(host.c_str());
        }
        
        if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            CLOSE_SOCKET(server_socket_);
            return false;
        }
        
        if (::listen(server_socket_, SOMAXCONN) < 0) {
            CLOSE_SOCKET(server_socket_);
            return false;
        }
        
        running_ = true;
        
        // Accept loop
        while (running_) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            SOCKET_TYPE client_socket = accept(server_socket_,
                reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
            
            if (client_socket == INVALID_SOCKET) {
                continue;
            }
            
            // Handle in thread
            std::thread([this, client_socket]() {
                handle_client(client_socket);
            }).detach();
        }
        
        return true;
    }
    
    void stop() {
        running_ = false;
        if (server_socket_ != INVALID_SOCKET) {
            CLOSE_SOCKET(server_socket_);
            server_socket_ = INVALID_SOCKET;
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }
    
    [[nodiscard]] bool is_running() const noexcept { return running_; }
    [[nodiscard]] int port() const noexcept { return port_; }

private:
    std::vector<Route> routes_;
    std::string host_;
    int port_ = 0;
    SOCKET_TYPE server_socket_ = INVALID_SOCKET;
    bool running_ = false;
    bool cors_enabled_ = false;
    std::string cors_origins_ = "*";
    
    void handle_client(SOCKET_TYPE client_socket) {
        // Read request
        std::string raw_request;
        char buffer[4096];
        int bytes;
        
        // Read headers first
        while ((bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes] = '\0';
            raw_request += buffer;
            
            // Check if we have complete headers
            if (raw_request.find("\r\n\r\n") != std::string::npos) {
                break;
            }
        }
        
        // Parse request
        Request req = parse_request(raw_request);
        
        // Read body if Content-Length present
        auto cl_it = req.headers.find("Content-Length");
        if (cl_it != req.headers.end()) {
            int content_length = std::stoi(cl_it->second);
            
            // Body might already be partially in raw_request
            auto body_start = raw_request.find("\r\n\r\n");
            if (body_start != std::string::npos) {
                req.body = raw_request.substr(body_start + 4);
            }
            
            // Read remaining body
            while (static_cast<int>(req.body.length()) < content_length) {
                bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                if (bytes <= 0) break;
                buffer[bytes] = '\0';
                req.body += buffer;
            }
        }
        
        // Create response
        Response res;
        
        // Add CORS headers
        if (cors_enabled_) {
            res.headers["Access-Control-Allow-Origin"] = cors_origins_;
            res.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
            res.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";
        }
        
        // Handle OPTIONS preflight
        if (req.method == "OPTIONS" && cors_enabled_) {
            res.status = 204;
        } else {
            // Find matching route
            bool found = false;
            for (const auto& route : routes_) {
                if (route.method == req.method && match_pattern(route.pattern, req.path)) {
                    route.handler(req, res);
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                res.status = 404;
                res.set_json("{\"error\": \"Not Found\"}");
            }
        }
        
        // Send response
        std::string response_str = build_response(res);
        send(client_socket, response_str.c_str(), 
             static_cast<int>(response_str.length()), 0);
        
        CLOSE_SOCKET(client_socket);
    }
    
    Request parse_request(const std::string& raw) {
        Request req;
        std::istringstream ss(raw);
        
        // Parse request line
        std::string line;
        if (std::getline(ss, line)) {
            std::istringstream line_ss(line);
            std::string path_with_query;
            line_ss >> req.method >> path_with_query;
            
            // Split path and query string
            auto q_pos = path_with_query.find('?');
            if (q_pos != std::string::npos) {
                req.path = path_with_query.substr(0, q_pos);
                req.query_string = path_with_query.substr(q_pos + 1);
                parse_query_string(req.query_string, req.params);
            } else {
                req.path = path_with_query;
            }
        }
        
        // Parse headers
        while (std::getline(ss, line) && line != "\r" && !line.empty()) {
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 1);
                // Trim
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                req.headers[key] = value;
            }
        }
        
        return req;
    }
    
    void parse_query_string(const std::string& query, 
                            std::map<std::string, std::string>& params) {
        std::istringstream ss(query);
        std::string pair;
        
        while (std::getline(ss, pair, '&')) {
            auto eq = pair.find('=');
            if (eq != std::string::npos) {
                std::string key = url_decode(pair.substr(0, eq));
                std::string value = url_decode(pair.substr(eq + 1));
                params[key] = value;
            }
        }
    }
    
    std::string url_decode(const std::string& str) {
        std::string result;
        for (size_t i = 0; i < str.length(); ++i) {
            if (str[i] == '%' && i + 2 < str.length()) {
                int val;
                std::istringstream hex(str.substr(i + 1, 2));
                hex >> std::hex >> val;
                result += static_cast<char>(val);
                i += 2;
            } else if (str[i] == '+') {
                result += ' ';
            } else {
                result += str[i];
            }
        }
        return result;
    }
    
    bool match_pattern(const std::string& pattern, const std::string& path) {
        // Simple prefix matching for now
        // TODO: Add path parameter support like /api/user/:id
        return pattern == path || 
               (pattern.back() == '*' && 
                path.substr(0, pattern.length() - 1) == pattern.substr(0, pattern.length() - 1));
    }
    
    std::string build_response(const Response& res) {
        std::ostringstream ss;
        
        // Status line
        ss << "HTTP/1.1 " << res.status << " " << status_text(res.status) << "\r\n";
        
        // Headers
        for (const auto& [key, value] : res.headers) {
            ss << key << ": " << value << "\r\n";
        }
        
        // Content-Length
        ss << "Content-Length: " << res.body.length() << "\r\n";
        ss << "Connection: close\r\n";
        ss << "\r\n";
        
        // Body
        ss << res.body;
        
        return ss.str();
    }
    
    static const char* status_text(int status) {
        switch (status) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 500: return "Internal Server Error";
            default: return "Unknown";
        }
    }
};

} // namespace payoff::http
