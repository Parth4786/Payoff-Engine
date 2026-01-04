/**
 * @file test_payoff.cpp
 * @brief Unit tests for payoff calculations
 */

#include "payoff/pricing.hpp"
#include "payoff/calculator.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace payoff::engine;

bool approx_equal(double a, double b, double epsilon = 0.01) {
    return std::abs(a - b) < epsilon;
}

void test_black_scholes_call() {
    std::cout << "Testing Black-Scholes Call..." << std::endl;
    
    PricingParams params;
    params.spot = 100.0;
    params.strike = 100.0;
    params.time_to_expiry = 1.0;  // 1 year
    params.volatility = 0.20;     // 20%
    params.risk_free_rate = 0.05;
    
    double price = bs_call_price(params);
    
    // Expected ATM call price around 10.45 for these params
    assert(price > 9.0 && price < 12.0);
    
    std::cout << "  ATM Call Price: " << price << std::endl;
    std::cout << "  ✓ Black-Scholes Call test passed" << std::endl;
}

void test_black_scholes_put() {
    std::cout << "Testing Black-Scholes Put..." << std::endl;
    
    PricingParams params;
    params.spot = 100.0;
    params.strike = 100.0;
    params.time_to_expiry = 1.0;
    params.volatility = 0.20;
    params.risk_free_rate = 0.05;
    
    double price = bs_put_price(params);
    
    // Expected ATM put price around 5.57 for these params
    assert(price > 4.0 && price < 8.0);
    
    std::cout << "  ATM Put Price: " << price << std::endl;
    std::cout << "  ✓ Black-Scholes Put test passed" << std::endl;
}

void test_put_call_parity() {
    std::cout << "Testing Put-Call Parity..." << std::endl;
    
    PricingParams params;
    params.spot = 100.0;
    params.strike = 100.0;
    params.time_to_expiry = 1.0;
    params.volatility = 0.20;
    params.risk_free_rate = 0.05;
    
    double call = bs_call_price(params);
    double put = bs_put_price(params);
    
    // Put-Call Parity: C - P = S - K*e^(-rT)
    double lhs = call - put;
    double rhs = params.spot - params.strike * std::exp(-params.risk_free_rate * params.time_to_expiry);
    
    assert(approx_equal(lhs, rhs, 0.001));
    
    std::cout << "  C - P = " << lhs << std::endl;
    std::cout << "  S - Ke^(-rT) = " << rhs << std::endl;
    std::cout << "  ✓ Put-Call Parity test passed" << std::endl;
}

void test_delta() {
    std::cout << "Testing Delta calculation..." << std::endl;
    
    PricingParams params;
    params.spot = 100.0;
    params.strike = 100.0;
    params.time_to_expiry = 0.25;  // 3 months
    params.volatility = 0.20;
    params.risk_free_rate = 0.05;
    
    double call_delta = calculate_delta(params, OptionType::Call);
    double put_delta = calculate_delta(params, OptionType::Put);
    
    // ATM call delta ~0.55, put delta ~-0.45
    assert(call_delta > 0.5 && call_delta < 0.6);
    assert(put_delta > -0.5 && put_delta < -0.4);
    
    // Delta relationship: call_delta - put_delta ≈ 1
    assert(approx_equal(call_delta - put_delta, 1.0, 0.1));
    
    std::cout << "  Call Delta: " << call_delta << std::endl;
    std::cout << "  Put Delta: " << put_delta << std::endl;
    std::cout << "  ✓ Delta test passed" << std::endl;
}

void test_gamma() {
    std::cout << "Testing Gamma calculation..." << std::endl;
    
    PricingParams params;
    params.spot = 100.0;
    params.strike = 100.0;
    params.time_to_expiry = 0.25;
    params.volatility = 0.20;
    params.risk_free_rate = 0.05;
    
    double gamma = calculate_gamma(params);
    
    // ATM gamma should be positive
    assert(gamma > 0);
    assert(gamma < 0.1);
    
    std::cout << "  Gamma: " << gamma << std::endl;
    std::cout << "  ✓ Gamma test passed" << std::endl;
}

void test_expiry_payoff() {
    std::cout << "Testing Expiry Payoff calculation..." << std::endl;
    
    // Create a long call
    Strategy strategy;
    strategy.name = "Long Call";
    strategy.underlying = "TEST";
    strategy.underlying_price = 100.0;
    
    OptionLeg call;
    call.type = OptionType::Call;
    call.side = Side::Buy;
    call.strike = 100.0;
    call.quantity = 1;
    call.lot_size = 1;
    call.premium = 5.0;
    strategy.legs.push_back(call);
    
    PayoffCalculator calculator;
    auto curve = calculator.calculate_expiry_payoff(strategy);
    
    // Max loss = premium paid
    assert(approx_equal(curve.max_loss, -5.0, 0.5));
    
    // Max profit is unlimited (but capped by our range)
    assert(curve.max_profit > 10.0);
    
    // Breakeven at strike + premium
    assert(!curve.breakevens.empty());
    assert(approx_equal(curve.breakevens[0], 105.0, 0.5));
    
    std::cout << "  Max Loss: " << curve.max_loss << std::endl;
    std::cout << "  Max Profit: " << curve.max_profit << std::endl;
    std::cout << "  Breakeven: " << curve.breakevens[0] << std::endl;
    std::cout << "  ✓ Expiry Payoff test passed" << std::endl;
}

void test_iv_calculation() {
    std::cout << "Testing Implied Volatility calculation..." << std::endl;
    
    PricingParams params;
    params.spot = 100.0;
    params.strike = 100.0;
    params.time_to_expiry = 0.25;
    params.volatility = 0.20;
    params.risk_free_rate = 0.05;
    
    // Get theoretical price at 20% IV
    double target_price = bs_call_price(params);
    
    // Try to recover IV from price
    auto recovered_iv = calculate_iv(target_price, params, OptionType::Call);
    
    assert(recovered_iv.has_value());
    assert(approx_equal(recovered_iv.value(), 0.20, 0.001));
    
    std::cout << "  Target Price: " << target_price << std::endl;
    std::cout << "  Recovered IV: " << (recovered_iv.value() * 100) << "%" << std::endl;
    std::cout << "  ✓ IV calculation test passed" << std::endl;
}

int main() {
    std::cout << "\n=== Payoff Engine Unit Tests ===\n" << std::endl;
    
    test_black_scholes_call();
    test_black_scholes_put();
    test_put_call_parity();
    test_delta();
    test_gamma();
    test_expiry_payoff();
    test_iv_calculation();
    
    std::cout << "\n✓ All payoff tests passed!\n" << std::endl;
    return 0;
}
