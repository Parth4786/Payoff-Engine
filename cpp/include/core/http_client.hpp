/**
 * @file http_client.hpp
 * @brief Real HTTP/HTTPS client using WinHTTP on Windows
 * 
 * This provides ACTUAL network connectivity for Kite API.
 * Uses WinHTTP which natively supports TLS/SSL.
 */

#ifndef PAYOFF_CORE_HTTP_CLIENT_HPP
#define PAYOFF_CORE_HTTP_CLIENT_HPP

#include <string>
#include <unordered_map>
#include <optional>
#include <functional>

namespace payoff::core {

/**
 * HTTP response container
 */
struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::string error_message;
    
    bool ok() const { return status_code >= 200 && status_code < 300; }
    bool is_error() const { return status_code == 0 || !error_message.empty(); }
};

/**
 * HTTP client with SSL support
 * Uses WinHTTP on Windows for native TLS support
 */
class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    // Non-copyable
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    
    /**
     * Set base URL for all requests
     * @param url Base URL (e.g., "https://api.kite.trade")
     */
    void set_base_url(const std::string& url);
    
    /**
     * Set default header for all requests
     */
    void set_header(const std::string& key, const std::string& value);
    
    /**
     * Set connection timeout in milliseconds
     */
    void set_timeout(int timeout_ms);
    
    /**
     * HTTP GET request
     * @param path URL path (appended to base_url)
     * @param headers Additional headers for this request
     * @return HTTP response
     */
    HttpResponse get(
        const std::string& path,
        const std::unordered_map<std::string, std::string>& headers = {});
    
    /**
     * HTTP POST request with form data
     * @param path URL path
     * @param form_data Form parameters
     * @param headers Additional headers
     * @return HTTP response
     */
    HttpResponse post(
        const std::string& path,
        const std::unordered_map<std::string, std::string>& form_data,
        const std::unordered_map<std::string, std::string>& headers = {});
    
    /**
     * HTTP POST request with JSON body
     * @param path URL path
     * @param json_body JSON string body
     * @param headers Additional headers
     * @return HTTP response
     */
    HttpResponse post_json(
        const std::string& path,
        const std::string& json_body,
        const std::unordered_map<std::string, std::string>& headers = {});
    
    /**
     * HTTP DELETE request
     */
    HttpResponse del(
        const std::string& path,
        const std::unordered_map<std::string, std::string>& headers = {});
    
    /**
     * URL encode a string
     */
    static std::string url_encode(const std::string& value);
    
    /**
     * Build query string from parameters
     */
    static std::string build_query_string(
        const std::unordered_map<std::string, std::string>& params);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    std::string base_url_;
    std::unordered_map<std::string, std::string> default_headers_;
    int timeout_ms_ = 30000;  // 30 seconds default
    
    HttpResponse make_request(
        const std::string& method,
        const std::string& path,
        const std::string& body,
        const std::string& content_type,
        const std::unordered_map<std::string, std::string>& headers);
};

/**
 * Compute SHA256 hash and return as hex string
 * Used for Kite token authentication
 */
std::string sha256_hex(const std::string& input);

} // namespace payoff::core

#endif // PAYOFF_CORE_HTTP_CLIENT_HPP
