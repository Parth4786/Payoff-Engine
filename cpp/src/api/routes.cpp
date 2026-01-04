/**
 * @file routes.cpp
 * @brief API route handlers - Utility functions for REST endpoints
 * 
 * Contains helper functions used by rest_server.cpp
 */

#include "core/models.hpp"
#include "payoff/models.hpp"
#include <sstream>
#include <string>
#include <vector>

namespace payoff::api {

// ============================================================================
// Query Parameter Parsing
// ============================================================================

/**
 * @brief Parse comma-separated values from string
 */
std::vector<std::string> parse_csv_values(const std::string& str) {
    std::vector<std::string> result;
    if (str.empty()) return result;
    
    std::istringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // Trim whitespace
        size_t start = item.find_first_not_of(" \t");
        size_t end = item.find_last_not_of(" \t");
        if (start != std::string::npos) {
            result.push_back(item.substr(start, end - start + 1));
        }
    }
    return result;
}

/**
 * @brief Parse instrument tokens from string (comma-separated integers)
 */
std::vector<uint32_t> parse_instrument_tokens(const std::string& str) {
    std::vector<uint32_t> result;
    if (str.empty()) return result;
    
    std::istringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            result.push_back(static_cast<uint32_t>(std::stoul(item)));
        } catch (...) {
            // Skip invalid tokens
        }
    }
    return result;
}

/**
 * @brief Parse strikes from string (comma-separated doubles)
 */
std::vector<double> parse_strikes(const std::string& str) {
    std::vector<double> result;
    if (str.empty()) return result;
    
    std::istringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            result.push_back(std::stod(item));
        } catch (...) {
            // Skip invalid values
        }
    }
    return result;
}

// ============================================================================
// Strategy Parsing from JSON (simplified)
// ============================================================================

/**
 * @brief Extract array of legs from JSON body (simplified parser)
 * 
 * Expected format:
 * {"legs": [
 *   {"type": "CE", "side": "BUY", "strike": 26300, "qty": 1, "lot": 25, "premium": 200},
 *   ...
 * ]}
 */
std::vector<engine::OptionLeg> parse_legs_from_json(const std::string& json) {
    std::vector<engine::OptionLeg> legs;
    
    // Very simplified parsing - look for each leg object
    // In production, use a proper JSON library
    
    size_t pos = json.find("\"legs\"");
    if (pos == std::string::npos) return legs;
    
    pos = json.find('[', pos);
    if (pos == std::string::npos) return legs;
    
    size_t end = json.find(']', pos);
    if (end == std::string::npos) return legs;
    
    std::string legs_section = json.substr(pos + 1, end - pos - 1);
    
    // Find each leg object {...}
    size_t leg_start = 0;
    while ((leg_start = legs_section.find('{', leg_start)) != std::string::npos) {
        size_t leg_end = legs_section.find('}', leg_start);
        if (leg_end == std::string::npos) break;
        
        std::string leg_json = legs_section.substr(leg_start, leg_end - leg_start + 1);
        
        engine::OptionLeg leg;
        
        // Parse type
        if (leg_json.find("\"PE\"") != std::string::npos || 
            leg_json.find("\"put\"") != std::string::npos) {
            leg.type = engine::OptionType::Put;
        } else {
            leg.type = engine::OptionType::Call;
        }
        
        // Parse side
        if (leg_json.find("\"SELL\"") != std::string::npos || 
            leg_json.find("\"sell\"") != std::string::npos) {
            leg.side = engine::Side::Sell;
        } else {
            leg.side = engine::Side::Buy;
        }
        
        // Parse strike
        auto strike_pos = leg_json.find("\"strike\"");
        if (strike_pos != std::string::npos) {
            size_t colon = leg_json.find(':', strike_pos);
            if (colon != std::string::npos) {
                size_t val_end = leg_json.find_first_of(",}", colon);
                try {
                    leg.strike = std::stod(leg_json.substr(colon + 1, val_end - colon - 1));
                } catch (...) {}
            }
        }
        
        // Parse quantity
        auto qty_pos = leg_json.find("\"qty\"");
        if (qty_pos == std::string::npos) qty_pos = leg_json.find("\"quantity\"");
        if (qty_pos != std::string::npos) {
            size_t colon = leg_json.find(':', qty_pos);
            if (colon != std::string::npos) {
                size_t val_end = leg_json.find_first_of(",}", colon);
                try {
                    leg.quantity = std::stoi(leg_json.substr(colon + 1, val_end - colon - 1));
                } catch (...) {}
            }
        }
        
        // Parse lot_size
        auto lot_pos = leg_json.find("\"lot\"");
        if (lot_pos == std::string::npos) lot_pos = leg_json.find("\"lot_size\"");
        if (lot_pos != std::string::npos) {
            size_t colon = leg_json.find(':', lot_pos);
            if (colon != std::string::npos) {
                size_t val_end = leg_json.find_first_of(",}", colon);
                try {
                    leg.lot_size = std::stoi(leg_json.substr(colon + 1, val_end - colon - 1));
                } catch (...) {}
            }
        }
        
        // Parse premium
        auto prem_pos = leg_json.find("\"premium\"");
        if (prem_pos != std::string::npos) {
            size_t colon = leg_json.find(':', prem_pos);
            if (colon != std::string::npos) {
                size_t val_end = leg_json.find_first_of(",}", colon);
                try {
                    leg.premium = std::stod(leg_json.substr(colon + 1, val_end - colon - 1));
                } catch (...) {}
            }
        }
        
        if (leg.strike > 0) {
            legs.push_back(leg);
        }
        
        leg_start = leg_end + 1;
    }
    
    return legs;
}

// ============================================================================
// Response Formatting
// ============================================================================

/**
 * @brief Format error response
 */
std::string error_response(int code, const std::string& message) {
    std::ostringstream ss;
    ss << "{\"error\":{\"code\":" << code 
       << ",\"message\":\"" << message << "\"}}";
    return ss.str();
}

/**
 * @brief Format success response with data
 */
std::string success_response(const std::string& data) {
    std::ostringstream ss;
    ss << "{\"success\":true,\"data\":" << data << "}";
    return ss.str();
}

} // namespace payoff::api
