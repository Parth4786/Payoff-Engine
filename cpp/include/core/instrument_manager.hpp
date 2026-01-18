#pragma once
/**
 * @file instrument_manager.hpp
 * @brief Instrument master data management
 * 
 * Handles:
 * - Loading instrument CSVs from Kite/broker
 * - Token → InstrumentInfo lookup (O(1))
 * - Symbol resolution and canonical format
 * - Lot size, tick size, expiry, strike info
 * 
 * Reference: docs/DeskMetrics/instrument_manager_deep_dive.md
 */

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace payoff::core {

// ============================================================================
// Instrument Types
// ============================================================================
enum class InstrumentType : uint8_t {
    Unknown = 0,
    EQ = 1,      // Equity
    FUT = 2,     // Futures
    CE = 3,      // Call Option
    PE = 4       // Put Option
};

enum class Exchange : uint8_t {
    Unknown = 0,
    NSE = 1,
    NFO = 2,
    BSE = 3,
    BFO = 4,
    CDS = 5,
    MCX = 6
};

constexpr const char* exchange_to_string(Exchange ex) {
    switch (ex) {
        case Exchange::NSE: return "NSE";
        case Exchange::NFO: return "NFO";
        case Exchange::BSE: return "BSE";
        case Exchange::BFO: return "BFO";
        case Exchange::CDS: return "CDS";
        case Exchange::MCX: return "MCX";
        default: return "UNKNOWN";
    }
}

Exchange exchange_from_string(std::string_view s);

// ============================================================================
// InstrumentInfo - Complete instrument definition
// ============================================================================
struct InstrumentInfo {
    // Primary keys
    uint32_t instrument_token = 0;   // Kite instrument token
    uint32_t exchange_token = 0;     // Exchange token (THIS IS instrument_id)
    
    // Identification
    std::string tradingsymbol;       // e.g., "NIFTY25JAN23000CE"
    std::string name;                // Company/Index name
    Exchange exchange = Exchange::Unknown;
    InstrumentType instrument_type = InstrumentType::Unknown;
    
    // Contract specs (for derivatives)
    std::optional<double> strike;
    std::optional<std::chrono::year_month_day> expiry;
    std::string underlying;          // Underlying symbol (e.g., "NIFTY")
    uint32_t underlying_token = 0;   // Underlying's exchange_token
    
    // Trading specs
    int32_t lot_size = 1;
    double tick_size = 0.05;
    
    // Segment info
    std::string segment;             // e.g., "NFO-OPT"
    
    // ========================================================================
    // Computed Properties
    // ========================================================================
    
    /**
     * @brief Get canonical symbol format: "{exchange}:{exchange_token}"
     */
    [[nodiscard]] std::string canonical_symbol() const {
        return std::string(exchange_to_string(exchange)) + ":" + 
               std::to_string(exchange_token);
    }
    
    [[nodiscard]] bool is_option() const noexcept {
        return instrument_type == InstrumentType::CE || 
               instrument_type == InstrumentType::PE;
    }
    
    [[nodiscard]] bool is_futures() const noexcept {
        return instrument_type == InstrumentType::FUT;
    }
    
    [[nodiscard]] bool is_equity() const noexcept {
        return instrument_type == InstrumentType::EQ;
    }
    
    [[nodiscard]] bool is_derivative() const noexcept {
        return is_option() || is_futures();
    }
    
    [[nodiscard]] bool is_call() const noexcept {
        return instrument_type == InstrumentType::CE;
    }
    
    [[nodiscard]] bool is_put() const noexcept {
        return instrument_type == InstrumentType::PE;
    }
};

// ============================================================================
// InstrumentManager - Central instrument lookup service
// ============================================================================
class InstrumentManager {
public:
    InstrumentManager() = default;
    ~InstrumentManager() = default;
    
    // Non-copyable, movable
    InstrumentManager(const InstrumentManager&) = delete;
    InstrumentManager& operator=(const InstrumentManager&) = delete;
    InstrumentManager(InstrumentManager&&) = default;
    InstrumentManager& operator=(InstrumentManager&&) = default;
    
    // ========================================================================
    // Loading
    // ========================================================================
    
    /**
     * @brief Load instruments from CSV file
     * @param filepath Path to Kite-format instrument CSV
     * @param exchange Exchange to tag instruments with
     * @return Number of instruments loaded
     */
    size_t load_csv(const std::string& filepath, Exchange exchange);
    
    /**
     * @brief Load all instrument CSVs from a directory
     * @param directory Directory containing *_instruments.csv files
     * @return Total number of instruments loaded
     */
    size_t load_directory(const std::string& directory);
    
    /**
     * @brief Load instruments from ClickHouse instrument dump
     * 
     * Queries ClickHouse for instrument master data. This is a fallback
     * when local CSV files are not available.
     * 
     * Expected table schema:
     *   - instrument_token: UInt32
     *   - exchange_token: UInt32
     *   - tradingsymbol: String
     *   - name: String
     *   - exchange: String
     *   - segment: String
     *   - lot_size: UInt32
     *   - tick_size: Float64
     *   - expiry: Nullable(Date)
     *   - strike: Nullable(Float64)
     * 
     * @param database ClickHouse database name
     * @param table Table name (default: "instruments")
     * @return Number of instruments loaded
     */
    size_t load_from_clickhouse(const std::string& database = "",
                                const std::string& table = "instruments");
    
    /**
     * @brief Load instruments from Kite API live
     * 
     * Fetches instrument master for all configured exchanges from Kite API.
     * Requires authenticated KiteClient.
     * 
     * Python equivalent:
     *   for exchange in ['NSE', 'NFO', 'BSE', 'BFO', 'MCX']:
     *       instruments.append(kite.instruments(exchange))
     * 
     * @param exchanges List of exchanges to fetch (default: NSE, NFO, BSE, BFO, MCX)
     * @return Number of instruments loaded
     */
    size_t load_from_kite(const std::vector<std::string>& exchanges = 
                          {"NSE", "NFO", "BSE", "BFO", "MCX"});
    
    /**
     * @brief Load instruments with fallback: Kite API first, then ClickHouse
     * 
     * Strategy:
     * 1. If Kite is authenticated, try load_from_kite() for all exchanges
     * 2. If no instruments loaded (or not authenticated), try load_from_clickhouse()
     * 
     * @return Total number of instruments loaded
     */
    size_t load_with_fallback();
    
    /**
     * @brief Clear all loaded instruments
     */
    void clear() noexcept;
    
    // ========================================================================
    // Lookup - O(1) operations
    // ========================================================================
    
    /**
     * @brief Resolve instrument by exchange_token (primary lookup)
     * @return Pointer to instrument info or nullptr if not found
     */
    [[nodiscard]] const InstrumentInfo* resolve(uint32_t exchange_token) const noexcept;
    
    /**
     * @brief Resolve by instrument_token (Kite's internal token)
     */
    [[nodiscard]] const InstrumentInfo* resolve_by_instrument_token(
        uint32_t instrument_token) const noexcept;
    
    /**
     * @brief Resolve by canonical symbol (e.g., "NFO:49543")
     */
    [[nodiscard]] const InstrumentInfo* resolve_by_canonical(
        std::string_view canonical_symbol) const noexcept;
    
    /**
     * @brief Resolve by trading symbol (may return multiple for same symbol across exchanges)
     */
    [[nodiscard]] std::vector<const InstrumentInfo*> resolve_by_tradingsymbol(
        std::string_view tradingsymbol) const;
    
    // ========================================================================
    // Query
    // ========================================================================
    
    /**
     * @brief Get all instruments for a given underlying
     */
    [[nodiscard]] std::vector<const InstrumentInfo*> get_by_underlying(
        std::string_view underlying) const;
    
    /**
     * @brief Get option chain for underlying
     * @param underlying The underlying symbol (e.g., "NIFTY")
     * @param expiry Optional expiry filter
     */
    [[nodiscard]] std::vector<const InstrumentInfo*> get_option_chain(
        std::string_view underlying,
        std::optional<std::chrono::year_month_day> expiry = std::nullopt) const;
    
    /**
     * @brief Get all futures for underlying
     */
    [[nodiscard]] std::vector<const InstrumentInfo*> get_futures(
        std::string_view underlying) const;
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    [[nodiscard]] size_t size() const noexcept { return instruments_.size(); }
    [[nodiscard]] bool empty() const noexcept { return instruments_.empty(); }
    
    /**
     * @brief Get count by exchange
     */
    [[nodiscard]] size_t count_by_exchange(Exchange ex) const noexcept;
    
    /**
     * @brief Get all unique underlyings
     */
    [[nodiscard]] std::vector<std::string> get_underlyings() const;

private:
    // Primary storage
    std::vector<InstrumentInfo> instruments_;
    
    // Lookup indices
    std::unordered_map<uint32_t, size_t> by_exchange_token_;
    std::unordered_map<uint32_t, size_t> by_instrument_token_;
    std::unordered_map<std::string, size_t> by_canonical_;
    std::unordered_multimap<std::string, size_t> by_tradingsymbol_;
    std::unordered_multimap<std::string, size_t> by_underlying_;
    
    void build_indices();
    static InstrumentType parse_instrument_type(std::string_view segment, 
                                                 std::string_view tradingsymbol);
};

// ============================================================================
// Global Instance (optional singleton pattern)
// ============================================================================

/**
 * @brief Get the global instrument manager instance
 * @note Initialize before use with load_csv or load_directory
 */
InstrumentManager& get_instrument_manager();

} // namespace payoff::core
