# Margin System — Deep Dive

> Technical documentation for translating the Python margin calculation system to C++.

---

## Overview

The Margin System calculates required margin for options/futures strategies. It has two modes:

1. **Kite API Mode** — Uses broker's official margin calculator
2. **Fallback Mode** — Rule-based estimation when API unavailable

---

## 1. Data Structures

### Python Source (`margin.py:20-78`)

### C++ Translation

```cpp
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace deskmetrics::domain::margin {

// Order for margin calculation
struct MarginOrder {
    std::string exchange;          // "NFO", "NSE", etc.
    std::string tradingsymbol;     // "NIFTY25JANFUT"
    std::string transaction_type;  // "BUY" or "SELL"
    int quantity{0};
    std::string product;           // "MIS" (intraday), "NRML" (overnight), "CNC" (delivery)
    std::string order_type{"MARKET"};
    std::optional<double> price;
    std::optional<double> trigger_price;
    std::optional<std::string> variety;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
        MarginOrder,
        exchange, tradingsymbol, transaction_type, quantity,
        product, order_type, price, trigger_price, variety
    )
};

// Margin breakdown components
struct MarginBreakdown {
    double total{0};
    double span{0};              // SPAN margin (portfolio risk)
    double exposure{0};          // Exposure margin (additional cushion)
    double option_premium{0};    // Premium for option buys
    std::optional<double> additional;
    std::optional<double> var;   // Value at Risk
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
        MarginBreakdown,
        total, span, exposure, option_premium, additional, var
    )
};

// Per-leg margin info
struct PerLegMargin {
    std::string tradingsymbol;
    double margin{0};
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PerLegMargin, tradingsymbol, margin)
};

// Complete margin response
struct BasketMarginResponse {
    MarginBreakdown initial;     // Before spread benefit
    MarginBreakdown final;       // After spread benefit
    std::string source;          // "kite" or "estimated"
    std::optional<std::vector<PerLegMargin>> per_leg;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
        BasketMarginResponse,
        initial, final, source, per_leg
    )
};

} // namespace
```

---

## 2. Fallback Margin Calculator

### Python Source (`margin.py:152-220`)

This is the **CRITICAL** function for offline/demo mode.

### Algorithm

```
FOR each order:
    1. Detect instrument type from symbol
       - is_option: ends with "CE" or "PE"
       - is_future: contains "FUT"
       - is_banknifty: contains "BANKNIFTY"
       - is_nifty: contains "NIFTY" but not BANKNIFTY
    
    2. Determine lot size
       - BANKNIFTY: 35
       - NIFTY: 75
       - Default: 50
    
    3. Calculate lots = quantity / lot_size
    
    4. Estimate margin based on type:
       - Option SELL: ~100k per lot (BANKNIFTY) or ~80k (NIFTY)
       - Option BUY: ~5k per lot (premium estimate)
       - Future BANKNIFTY: ~240k per lot
       - Future NIFTY: ~170k per lot
       - Future other: ~150k per lot
       - Equity: ~1500 per share
    
    5. Split into SPAN (60-70%) and Exposure (30-40%)

AFTER all orders:
    IF basket has both BUY and SELL legs:
        Apply spread benefit: total * 0.4 (60% reduction)
```

### C++ Implementation

```cpp
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include "models.hpp"

namespace deskmetrics::domain::margin {

// Constants for margin estimation
namespace estimates {
    // Lot sizes (approximate, subject to change)
    constexpr int LOT_SIZE_BANKNIFTY = 35;
    constexpr int LOT_SIZE_NIFTY = 75;
    constexpr int LOT_SIZE_DEFAULT = 50;
    
    // Option SELL margin per lot
    constexpr double OPTION_SELL_BANKNIFTY = 100000.0;  // ~1 lakh
    constexpr double OPTION_SELL_NIFTY = 80000.0;       // ~80k
    constexpr double OPTION_SELL_DEFAULT = 70000.0;     // ~70k
    
    // Option BUY premium per lot (rough estimate)
    constexpr double OPTION_BUY_PREMIUM = 5000.0;       // ~5k
    
    // Futures margin per lot
    constexpr double FUTURE_BANKNIFTY = 240000.0;       // ~2.4 lakh
    constexpr double FUTURE_NIFTY = 170000.0;           // ~1.7 lakh
    constexpr double FUTURE_DEFAULT = 150000.0;         // ~1.5 lakh
    
    // Equity margin per share (rough)
    constexpr double EQUITY_PER_SHARE = 1500.0;
    
    // Spread benefit (60% reduction when hedged)
    constexpr double SPREAD_BENEFIT_MULTIPLIER = 0.4;
    
    // SPAN vs Exposure split
    constexpr double SPAN_RATIO_OPTION = 0.6;
    constexpr double EXPOSURE_RATIO_OPTION = 0.4;
    constexpr double SPAN_RATIO_FUTURE = 0.7;
    constexpr double EXPOSURE_RATIO_FUTURE = 0.3;
}

// Symbol pattern detection
inline bool is_option(const std::string& symbol) {
    return symbol.length() >= 2 && 
           (symbol.substr(symbol.length() - 2) == "CE" ||
            symbol.substr(symbol.length() - 2) == "PE");
}

inline bool is_future(const std::string& symbol) {
    return symbol.find("FUT") != std::string::npos;
}

inline bool is_banknifty(const std::string& symbol) {
    return symbol.find("BANKNIFTY") != std::string::npos;
}

inline bool is_nifty(const std::string& symbol) {
    return symbol.find("NIFTY") != std::string::npos && !is_banknifty(symbol);
}

inline bool is_finnifty(const std::string& symbol) {
    return symbol.find("FINNIFTY") != std::string::npos;
}

// Get lot size for symbol
inline int get_lot_size(const std::string& symbol) {
    if (is_banknifty(symbol)) return estimates::LOT_SIZE_BANKNIFTY;
    if (is_nifty(symbol) || is_finnifty(symbol)) return estimates::LOT_SIZE_NIFTY;
    return estimates::LOT_SIZE_DEFAULT;
}

// Calculate fallback margin for a basket of orders
inline BasketMarginResponse calculate_fallback_margin(const std::vector<MarginOrder>& orders) {
    double total_margin = 0;
    double total_span = 0;
    double total_exposure = 0;
    double total_premium = 0;
    
    std::vector<PerLegMargin> per_leg;
    per_leg.reserve(orders.size());
    
    bool has_buys = false;
    bool has_sells = false;
    
    for (const auto& order : orders) {
        int qty = std::abs(order.quantity);
        if (qty == 0) continue;
        
        int lot_size = get_lot_size(order.tradingsymbol);
        int lots = std::max(1, qty / lot_size);
        
        double estimated = 0;
        
        if (is_option(order.tradingsymbol)) {
            if (order.transaction_type == "SELL") {
                has_sells = true;
                // Option SELL requires margin
                if (is_banknifty(order.tradingsymbol)) {
                    estimated = lots * estimates::OPTION_SELL_BANKNIFTY;
                } else if (is_nifty(order.tradingsymbol)) {
                    estimated = lots * estimates::OPTION_SELL_NIFTY;
                } else {
                    estimated = lots * estimates::OPTION_SELL_DEFAULT;
                }
                total_span += estimated * estimates::SPAN_RATIO_OPTION;
                total_exposure += estimated * estimates::EXPOSURE_RATIO_OPTION;
            } else {
                has_buys = true;
                // Option BUY just needs premium
                estimated = lots * estimates::OPTION_BUY_PREMIUM;
                total_premium += estimated;
            }
        } else if (is_future(order.tradingsymbol)) {
            if (order.transaction_type == "SELL") has_sells = true;
            else has_buys = true;
            
            // Futures require margin regardless of direction
            if (is_banknifty(order.tradingsymbol)) {
                estimated = lots * estimates::FUTURE_BANKNIFTY;
            } else if (is_nifty(order.tradingsymbol)) {
                estimated = lots * estimates::FUTURE_NIFTY;
            } else {
                estimated = lots * estimates::FUTURE_DEFAULT;
            }
            total_span += estimated * estimates::SPAN_RATIO_FUTURE;
            total_exposure += estimated * estimates::EXPOSURE_RATIO_FUTURE;
        } else {
            // Equity
            if (order.transaction_type == "SELL") has_sells = true;
            else has_buys = true;
            
            estimated = qty * estimates::EQUITY_PER_SHARE;
        }
        
        total_margin += estimated;
        per_leg.push_back({order.tradingsymbol, estimated});
    }
    
    // Build initial breakdown (before spread benefit)
    MarginBreakdown initial{
        .total = total_margin,
        .span = total_span,
        .exposure = total_exposure,
        .option_premium = total_premium,
    };
    
    // Apply spread benefit if hedged position
    MarginBreakdown final_margin = initial;
    
    if (has_buys && has_sells) {
        // 60% reduction for spreads
        final_margin.total *= estimates::SPREAD_BENEFIT_MULTIPLIER;
        final_margin.span *= estimates::SPREAD_BENEFIT_MULTIPLIER;
        final_margin.exposure *= estimates::SPREAD_BENEFIT_MULTIPLIER;
    }
    
    return BasketMarginResponse{
        .initial = initial,
        .final = final_margin,
        .source = "estimated",
        .per_leg = per_leg,
    };
}

} // namespace
```

---

## 3. Kite API Integration

### Python Source (`margin.py:223-310`, `kite_client.py`)

When Kite API is available, we call `basket_margins()` instead of estimating.

### C++ Implementation

```cpp
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "models.hpp"

namespace deskmetrics::infra::kite {

using json = nlohmann::json;

class KiteClient {
public:
    explicit KiteClient(std::string api_key, std::string access_token)
        : api_key_(std::move(api_key))
        , access_token_(std::move(access_token))
    {}
    
    // Check if client is configured
    bool is_available() const {
        return !api_key_.empty() && !access_token_.empty();
    }
    
    // Calculate basket margins via Kite API
    std::optional<domain::margin::BasketMarginResponse> basket_margins(
        const std::vector<domain::margin::MarginOrder>& orders
    ) {
        if (!is_available()) return std::nullopt;
        
        // Build request body
        json orders_json = json::array();
        for (const auto& order : orders) {
            json o;
            o["exchange"] = order.exchange;
            o["tradingsymbol"] = order.tradingsymbol;
            o["transaction_type"] = order.transaction_type;
            o["quantity"] = order.quantity;
            o["product"] = order.product;
            o["order_type"] = order.order_type;
            o["variety"] = order.variety.value_or("regular");
            if (order.price) o["price"] = *order.price;
            if (order.trigger_price) o["trigger_price"] = *order.trigger_price;
            orders_json.push_back(o);
        }
        
        // POST to Kite API
        auto response = cpr::Post(
            cpr::Url{"https://api.kite.trade/margins/basket"},
            cpr::Header{
                {"X-Kite-Version", "3"},
                {"Authorization", "token " + api_key_ + ":" + access_token_},
                {"Content-Type", "application/json"}
            },
            cpr::Body{orders_json.dump()}
        );
        
        if (response.status_code != 200) {
            return std::nullopt;
        }
        
        try {
            auto data = json::parse(response.text);
            return parse_margin_response(data, orders);
        } catch (...) {
            return std::nullopt;
        }
    }

private:
    std::string api_key_;
    std::string access_token_;
    
    static domain::margin::BasketMarginResponse parse_margin_response(
        const json& data,
        const std::vector<domain::margin::MarginOrder>& orders
    ) {
        domain::margin::BasketMarginResponse result;
        result.source = "kite";
        
        // Parse initial margin
        if (data.contains("initial")) {
            const auto& init = data["initial"];
            result.initial.total = init.value("total", 0.0);
            result.initial.span = init.value("span", 0.0);
            result.initial.exposure = init.value("exposure", 0.0);
            result.initial.option_premium = init.value("option_premium", 0.0);
            if (init.contains("additional") && !init["additional"].is_null()) {
                result.initial.additional = init["additional"].get<double>();
            }
            if (init.contains("var") && !init["var"].is_null()) {
                result.initial.var = init["var"].get<double>();
            }
        }
        
        // Parse final margin
        if (data.contains("final")) {
            const auto& fin = data["final"];
            result.final.total = fin.value("total", 0.0);
            result.final.span = fin.value("span", 0.0);
            result.final.exposure = fin.value("exposure", 0.0);
            result.final.option_premium = fin.value("option_premium", 0.0);
            if (fin.contains("additional") && !fin["additional"].is_null()) {
                result.final.additional = fin["additional"].get<double>();
            }
            if (fin.contains("var") && !fin["var"].is_null()) {
                result.final.var = fin["var"].get<double>();
            }
        }
        
        // Parse per-leg margins
        if (data.contains("orders") && data["orders"].is_array()) {
            result.per_leg = std::vector<domain::margin::PerLegMargin>{};
            for (size_t i = 0; i < data["orders"].size() && i < orders.size(); ++i) {
                result.per_leg->push_back({
                    orders[i].tradingsymbol,
                    data["orders"][i].value("total", 0.0)
                });
            }
        }
        
        return result;
    }
};

} // namespace
```

---

## 4. Margin Calculator Service

### Combined service that tries Kite first, falls back to estimation

```cpp
#pragma once
#include <vector>
#include <memory>
#include "models.hpp"
#include "fallback.hpp"
#include "../infra/kite/client.hpp"

namespace deskmetrics::domain::margin {

class MarginCalculator {
public:
    explicit MarginCalculator(std::shared_ptr<infra::kite::KiteClient> kite = nullptr)
        : kite_(std::move(kite))
    {}
    
    // Calculate margin for basket of orders
    BasketMarginResponse calculate(const std::vector<MarginOrder>& orders) {
        // Try Kite API first
        if (kite_ && kite_->is_available()) {
            auto result = kite_->basket_margins(orders);
            if (result) {
                return *result;
            }
        }
        
        // Fall back to estimation
        return calculate_fallback_margin(orders);
    }
    
    // Force fallback mode (for testing)
    BasketMarginResponse calculate_fallback(const std::vector<MarginOrder>& orders) {
        return calculate_fallback_margin(orders);
    }
    
    // Check if live margin is available
    bool has_live_margin() const {
        return kite_ && kite_->is_available();
    }

private:
    std::shared_ptr<infra::kite::KiteClient> kite_;
};

} // namespace
```

---

## 5. Testing

```cpp
// tests/test_margin.cpp

#include <catch2/catch_test_macros.hpp>
#include "margin/fallback.hpp"
#include "margin/models.hpp"

using namespace deskmetrics::domain::margin;

TEST_CASE("Fallback margin - single NIFTY future") {
    std::vector<MarginOrder> orders = {
        {
            .exchange = "NFO",
            .tradingsymbol = "NIFTY25JANFUT",
            .transaction_type = "BUY",
            .quantity = 75,
            .product = "NRML"
        }
    };
    
    auto result = calculate_fallback_margin(orders);
    
    REQUIRE(result.source == "estimated");
    REQUIRE(result.initial.total == Catch::Approx(170000.0));
    REQUIRE(result.final.total == result.initial.total);  // No spread benefit
}

TEST_CASE("Fallback margin - BANKNIFTY spread") {
    std::vector<MarginOrder> orders = {
        {
            .exchange = "NFO",
            .tradingsymbol = "BANKNIFTY25JANFUT",
            .transaction_type = "BUY",
            .quantity = 35,
            .product = "NRML"
        },
        {
            .exchange = "NFO",
            .tradingsymbol = "BANKNIFTY25JANFUT",
            .transaction_type = "SELL",
            .quantity = 35,
            .product = "NRML"
        }
    };
    
    auto result = calculate_fallback_margin(orders);
    
    // Should have spread benefit (40% of initial)
    REQUIRE(result.final.total == Catch::Approx(result.initial.total * 0.4));
}

TEST_CASE("Fallback margin - option sell") {
    std::vector<MarginOrder> orders = {
        {
            .exchange = "NFO",
            .tradingsymbol = "NIFTY2510224000CE",
            .transaction_type = "SELL",
            .quantity = 75,
            .product = "NRML"
        }
    };
    
    auto result = calculate_fallback_margin(orders);
    
    // ~80k per lot for NIFTY option sell
    REQUIRE(result.initial.total == Catch::Approx(80000.0));
}

TEST_CASE("Fallback margin - option buy (premium only)") {
    std::vector<MarginOrder> orders = {
        {
            .exchange = "NFO",
            .tradingsymbol = "NIFTY2510224000CE",
            .transaction_type = "BUY",
            .quantity = 75,
            .product = "NRML"
        }
    };
    
    auto result = calculate_fallback_margin(orders);
    
    // ~5k premium per lot
    REQUIRE(result.initial.total == Catch::Approx(5000.0));
    REQUIRE(result.initial.option_premium == Catch::Approx(5000.0));
}

TEST_CASE("Symbol detection - is_option") {
    REQUIRE(is_option("NIFTY2510224000CE"));
    REQUIRE(is_option("BANKNIFTY25O0948000PE"));
    REQUIRE_FALSE(is_option("NIFTY25JANFUT"));
    REQUIRE_FALSE(is_option("RELIANCE"));
}

TEST_CASE("Symbol detection - is_future") {
    REQUIRE(is_future("NIFTY25JANFUT"));
    REQUIRE(is_future("BANKNIFTY26FEBFUT"));
    REQUIRE_FALSE(is_future("NIFTY2510224000CE"));
}

TEST_CASE("Symbol detection - is_banknifty vs is_nifty") {
    REQUIRE(is_banknifty("BANKNIFTY25JANFUT"));
    REQUIRE(is_banknifty("BANKNIFTY25O0948000PE"));
    REQUIRE_FALSE(is_banknifty("NIFTY25JANFUT"));
    
    REQUIRE(is_nifty("NIFTY25JANFUT"));
    REQUIRE(is_nifty("NIFTY2510224000CE"));
    REQUIRE_FALSE(is_nifty("BANKNIFTY25JANFUT"));  // Should not match
}
```

---

## 6. Important Notes

### Margin Values Are ESTIMATES

The fallback margin values are rough approximations based on typical market conditions:

| Instrument | Estimated Margin | Real Range |
|------------|-----------------|------------|
| NIFTY Future | ₹1.7L/lot | ₹1.5L - ₹2.2L |
| BANKNIFTY Future | ₹2.4L/lot | ₹2.0L - ₹3.0L |
| NIFTY Option Sell | ₹80k/lot | ₹50k - ₹1.5L |
| BANKNIFTY Option Sell | ₹1.0L/lot | ₹60k - ₹2.0L |

**Real margin depends on:**
- Spot price
- Implied volatility
- Days to expiry
- Strike distance from spot
- Exchange SPAN calculations

### When to Use Fallback

- **Demo/testing**: Always works without API
- **API downtime**: Graceful degradation
- **Rate limiting**: Avoid excessive API calls

### When NOT to Use Fallback

- **Production trading decisions**: Always use Kite API
- **Risk calculations**: Estimates may be 50%+ off
- **Regulatory compliance**: Need accurate margins

---

## Files Summary

| C++ File | Corresponds to Python | Purpose |
|----------|----------------------|---------|
| `models.hpp` | `margin.py:20-78` | Data structures |
| `fallback.hpp` | `margin.py:152-220` | Fallback calculation |
| `kite/client.hpp` | `kite_client.py` | API wrapper |
| `calculator.hpp` | New | Combined service |

---

*Next: Build the Payoff Engine (NEW functionality not in Python)*
