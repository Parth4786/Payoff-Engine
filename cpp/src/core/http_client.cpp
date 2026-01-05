/**
 * @file http_client.cpp
 * @brief REAL HTTP/HTTPS client implementation using WinHTTP
 * 
 * This is NOT a stub - it provides actual network connectivity.
 * WinHTTP is a Windows API that natively supports HTTPS/TLS.
 */

#include "core/http_client.hpp"

#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#include <bcrypt.h>
#else
// For non-Windows, we'd use libcurl or similar
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/sha.h>
#endif

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace payoff::core {

// ============================================================================
// URL Encoding
// ============================================================================

std::string HttpClient::url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || 
            c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    
    return escaped.str();
}

std::string HttpClient::build_query_string(
    const std::unordered_map<std::string, std::string>& params) {
    
    std::string result;
    for (const auto& [key, value] : params) {
        if (!result.empty()) result += "&";
        result += url_encode(key) + "=" + url_encode(value);
    }
    return result;
}

// ============================================================================
// SHA256 Implementation
// ============================================================================

#ifdef _WIN32
std::string sha256_hex(const std::string& input) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD hashLength = 0;
    DWORD resultLength = 0;
    
    std::string result;
    
    // Open algorithm provider
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return "";
    }
    
    // Get hash size
    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&hashLength, 
                          sizeof(hashLength), &resultLength, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    std::vector<BYTE> hash(hashLength);
    
    // Create hash object
    if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    // Hash data
    if (BCryptHashData(hHash, (PBYTE)input.data(), (ULONG)input.size(), 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    // Finish hash
    if (BCryptFinishHash(hHash, hash.data(), hashLength, 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    // Convert to hex string
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (DWORD i = 0; i < hashLength; ++i) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    
    return oss.str();
}
#else
std::string sha256_hex(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), 
           input.size(), hash);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}
#endif

// ============================================================================
// WinHTTP Implementation
// ============================================================================

#ifdef _WIN32

struct HttpClient::Impl {
    HINTERNET hSession = nullptr;
    
    Impl() {
        // Create session with user agent
        hSession = WinHttpOpen(
            L"PayoffEngine/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
    }
    
    ~Impl() {
        if (hSession) {
            WinHttpCloseHandle(hSession);
        }
    }
};

HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;

void HttpClient::set_base_url(const std::string& url) {
    base_url_ = url;
}

void HttpClient::set_header(const std::string& key, const std::string& value) {
    default_headers_[key] = value;
}

void HttpClient::set_timeout(int timeout_ms) {
    timeout_ms_ = timeout_ms;
}

// Helper to convert string to wide string
static std::wstring to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return wstr;
}

// Helper to convert wide string to string
static std::string to_string(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
    return str;
}

// Parse URL into components
struct UrlComponents {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool is_https = true;
};

static UrlComponents parse_url(const std::string& url) {
    UrlComponents result;
    
    std::string u = url;
    
    // Check scheme
    if (u.find("https://") == 0) {
        result.is_https = true;
        result.port = INTERNET_DEFAULT_HTTPS_PORT;
        u = u.substr(8);
    } else if (u.find("http://") == 0) {
        result.is_https = false;
        result.port = INTERNET_DEFAULT_HTTP_PORT;
        u = u.substr(7);
    }
    
    // Find path
    auto slash_pos = u.find('/');
    std::string host_part;
    std::string path_part = "/";
    
    if (slash_pos != std::string::npos) {
        host_part = u.substr(0, slash_pos);
        path_part = u.substr(slash_pos);
    } else {
        host_part = u;
    }
    
    // Check for port in host
    auto colon_pos = host_part.find(':');
    if (colon_pos != std::string::npos) {
        result.port = static_cast<INTERNET_PORT>(
            std::stoi(host_part.substr(colon_pos + 1)));
        host_part = host_part.substr(0, colon_pos);
    }
    
    result.host = to_wstring(host_part);
    result.path = to_wstring(path_part);
    
    return result;
}

HttpResponse HttpClient::make_request(
    const std::string& method,
    const std::string& path,
    const std::string& body,
    const std::string& content_type,
    const std::unordered_map<std::string, std::string>& headers) {
    
    HttpResponse response;
    
    if (!impl_->hSession) {
        response.error_message = "WinHTTP session not initialized";
        return response;
    }
    
    // Build full URL
    std::string full_url = base_url_ + path;
    auto url = parse_url(full_url);
    
    // Connect to server
    HINTERNET hConnect = WinHttpConnect(
        impl_->hSession,
        url.host.c_str(),
        url.port,
        0);
    
    if (!hConnect) {
        response.error_message = "Failed to connect to server: " + std::to_string(GetLastError());
        return response;
    }
    
    // Create request
    std::wstring wmethod = to_wstring(method);
    DWORD flags = url.is_https ? WINHTTP_FLAG_SECURE : 0;
    
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        wmethod.c_str(),
        url.path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    
    if (!hRequest) {
        response.error_message = "Failed to create request: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hConnect);
        return response;
    }
    
    // Set timeouts
    WinHttpSetTimeouts(hRequest, timeout_ms_, timeout_ms_, timeout_ms_, timeout_ms_);
    
    // Build headers string
    std::wstring header_str;
    
    // Add default headers
    for (const auto& [key, value] : default_headers_) {
        header_str += to_wstring(key + ": " + value + "\r\n");
    }
    
    // Add request-specific headers
    for (const auto& [key, value] : headers) {
        header_str += to_wstring(key + ": " + value + "\r\n");
    }
    
    // Add content type if we have a body
    if (!body.empty() && !content_type.empty()) {
        header_str += to_wstring("Content-Type: " + content_type + "\r\n");
    }
    
    // Add headers
    if (!header_str.empty()) {
        WinHttpAddRequestHeaders(hRequest, header_str.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD);
    }
    
    // Send request
    BOOL result = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.c_str(),
        body.empty() ? 0 : static_cast<DWORD>(body.size()),
        body.empty() ? 0 : static_cast<DWORD>(body.size()),
        0);
    
    if (!result) {
        response.error_message = "Failed to send request: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return response;
    }
    
    // Receive response
    result = WinHttpReceiveResponse(hRequest, nullptr);
    if (!result) {
        response.error_message = "Failed to receive response: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        return response;
    }
    
    // Get status code
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(hRequest, 
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    response.status_code = static_cast<int>(status);
    
    // Read response body
    std::string body_data;
    DWORD bytes_available = 0;
    
    do {
        bytes_available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &bytes_available)) {
            break;
        }
        
        if (bytes_available == 0) break;
        
        std::vector<char> buffer(bytes_available + 1);
        DWORD bytes_read = 0;
        
        if (WinHttpReadData(hRequest, buffer.data(), bytes_available, &bytes_read)) {
            body_data.append(buffer.data(), bytes_read);
        }
    } while (bytes_available > 0);
    
    response.body = std::move(body_data);
    
    // Cleanup
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    
    return response;
}

HttpResponse HttpClient::get(
    const std::string& path,
    const std::unordered_map<std::string, std::string>& headers) {
    return make_request("GET", path, "", "", headers);
}

HttpResponse HttpClient::post(
    const std::string& path,
    const std::unordered_map<std::string, std::string>& form_data,
    const std::unordered_map<std::string, std::string>& headers) {
    
    std::string body = build_query_string(form_data);
    return make_request("POST", path, body, "application/x-www-form-urlencoded", headers);
}

HttpResponse HttpClient::post_json(
    const std::string& path,
    const std::string& json_body,
    const std::unordered_map<std::string, std::string>& headers) {
    return make_request("POST", path, json_body, "application/json", headers);
}

HttpResponse HttpClient::del(
    const std::string& path,
    const std::unordered_map<std::string, std::string>& headers) {
    return make_request("DELETE", path, "", "", headers);
}

#else
// Non-Windows implementation would go here (using libcurl or raw sockets with OpenSSL)
// For now, stub it out

struct HttpClient::Impl {};

HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;

void HttpClient::set_base_url(const std::string& url) { base_url_ = url; }
void HttpClient::set_header(const std::string& key, const std::string& value) { 
    default_headers_[key] = value; 
}
void HttpClient::set_timeout(int timeout_ms) { timeout_ms_ = timeout_ms; }

HttpResponse HttpClient::make_request(
    const std::string& method,
    const std::string& path,
    const std::string& body,
    const std::string& content_type,
    const std::unordered_map<std::string, std::string>& headers) {
    
    HttpResponse response;
    response.error_message = "Non-Windows build requires libcurl or OpenSSL";
    return response;
}

HttpResponse HttpClient::get(const std::string& path,
    const std::unordered_map<std::string, std::string>& headers) {
    return make_request("GET", path, "", "", headers);
}

HttpResponse HttpClient::post(const std::string& path,
    const std::unordered_map<std::string, std::string>& form_data,
    const std::unordered_map<std::string, std::string>& headers) {
    return make_request("POST", path, build_query_string(form_data), 
                        "application/x-www-form-urlencoded", headers);
}

HttpResponse HttpClient::post_json(const std::string& path,
    const std::string& json_body,
    const std::unordered_map<std::string, std::string>& headers) {
    return make_request("POST", path, json_body, "application/json", headers);
}

HttpResponse HttpClient::del(const std::string& path,
    const std::unordered_map<std::string, std::string>& headers) {
    return make_request("DELETE", path, "", "", headers);
}

#endif // _WIN32

} // namespace payoff::core
