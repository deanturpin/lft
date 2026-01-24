#pragma once

// Basis points (bps) utility functions
// 1 bp = 0.01% = 0.0001

// Constexpr tolerance helper for floating-point comparisons
constexpr bool near(double a, double b, double eps = 1e-12) {
  return (a > b ? a - b : b - a) <= eps;
}

// Convert price change to basis points
constexpr double price_change_to_bps(double price_change, double base_price) {
  return (price_change / base_price) * 10000.0;
}

// Convert basis points to price change
constexpr double bps_to_price_change(double bps, double base_price) {
  return (bps / 10000.0) * base_price;
}

// Convert percentage (0.0-1.0) to basis points
constexpr double percent_to_bps(double percent) {
  return percent * 10000.0;
}

// Convert basis points to percentage (0.0-1.0)
constexpr double bps_to_percent(double bps) {
  return bps / 10000.0;
}

// User-defined literal for basis points
constexpr double operator""_bps(long double x) {
  return x / 10000.0;  // Convert to decimal (100 bps = 0.01)
}

constexpr double operator""_bps(unsigned long long x) {
  return static_cast<double>(x) / 10000.0;
}

// Compile-time tests using near() for floating-point comparison
static_assert(near(price_change_to_bps(1.0, 100.0), 100.0), "1 on 100 = 100 bps = 1%");
static_assert(near(price_change_to_bps(0.5, 100.0), 50.0), "0.5 on 100 = 50 bps = 0.5%");
static_assert(near(price_change_to_bps(0.01, 100.0), 1.0), "0.01 on 100 = 1 bp");

static_assert(near(bps_to_price_change(100.0, 100.0), 1.0), "100 bps on 100 = 1");
static_assert(near(bps_to_price_change(50.0, 100.0), 0.5), "50 bps on 100 = 0.5");
static_assert(near(bps_to_price_change(1.0, 100.0), 0.01), "1 bp on 100 = 0.01");

static_assert(near(percent_to_bps(0.01), 100.0), "1% = 100 bps");
static_assert(near(percent_to_bps(0.001), 10.0), "0.1% = 10 bps");
static_assert(near(percent_to_bps(0.0001), 1.0), "0.01% = 1 bp");

static_assert(near(bps_to_percent(100.0), 0.01), "100 bps = 1%");
static_assert(near(bps_to_percent(10.0), 0.001), "10 bps = 0.1%");
static_assert(near(bps_to_percent(1.0), 0.0001), "1 bp = 0.01%");

static_assert(near(100_bps, 0.01), "100 bps literal = 0.01 (1%)");
static_assert(near(50_bps, 0.005), "50 bps literal = 0.005 (0.5%)");
static_assert(near(1_bps, 0.0001), "1 bp literal = 0.0001 (0.01%)");
