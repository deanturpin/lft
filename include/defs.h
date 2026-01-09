#pragma once

namespace lft {

// Trading parameters
constexpr auto notional_amount = 1000.0; // Dollar amount per trade
constexpr auto calibration_days = 30;    // Duration for strategy calibration

// Timing parameters
constexpr auto poll_interval_seconds = 65; // 60s bar + buffer
constexpr auto max_cycles = 60; // Run for 60 minutes then re-calibrate

// Alert thresholds
constexpr auto stock_alert_threshold = 2.0;  // Standard alert at 2% move
constexpr auto crypto_alert_threshold = 5.0; // Crypto is more volatile
constexpr auto outlier_threshold = 20.0;     // Extreme move requiring attention

// Alert helper functions
constexpr bool is_alert(double change_pct, bool is_crypto) {
  auto threshold = is_crypto ? crypto_alert_threshold : stock_alert_threshold;
  auto abs_change = (change_pct < 0.0) ? -change_pct : change_pct;
  return abs_change >= threshold;
}

constexpr bool is_outlier(double change_pct) {
  auto abs_change = (change_pct < 0.0) ? -change_pct : change_pct;
  return abs_change >= outlier_threshold;
}

// Compile-time tests for alert functions
static_assert(!is_alert(1.9, false), "Stock: 1.9% should not alert (< 2%)");
static_assert(is_alert(2.0, false), "Stock: 2.0% should alert (>= 2%)");
static_assert(is_alert(5.0, false), "Stock: 5.0% should alert");
static_assert(is_alert(-2.5, false),
              "Stock: -2.5% should alert (absolute value)");
static_assert(!is_alert(4.9, true), "Crypto: 4.9% should not alert (< 5%)");
static_assert(is_alert(5.0, true), "Crypto: 5.0% should alert (>= 5%)");
static_assert(is_alert(10.0, true), "Crypto: 10.0% should alert");
static_assert(is_alert(-7.0, true),
              "Crypto: -7.0% should alert (absolute value)");
static_assert(!is_outlier(19.9), "19.9% should not be outlier (< 20%)");
static_assert(is_outlier(20.0), "20.0% should be outlier (>= 20%)");
static_assert(is_outlier(50.0), "50.0% should be outlier");
static_assert(is_outlier(-25.0), "-25.0% should be outlier (absolute value)");
static_assert(!is_outlier(0.0), "0% should not be outlier");
static_assert(!is_alert(0.0, false), "0% should not alert (stocks)");
static_assert(!is_alert(0.0, true), "0% should not alert (crypto)");

// Compile-time safety checks
static_assert(notional_amount > 0.0, "Trade size must be positive");
static_assert(notional_amount >= 1.0, "Trade size too small - minimum $1");
static_assert(notional_amount <= 100000.0,
              "Trade size dangerously high - max $100k per trade");
static_assert(calibration_days > 0, "Calibration period must be positive");
static_assert(calibration_days >= 7,
              "Calibration period too short - minimum 7 days");
static_assert(calibration_days <= 365,
              "Calibration period too long - max 1 year");
static_assert(poll_interval_seconds >= 60,
              "Poll interval too short - minimum 60s for 1Min bars");
static_assert(poll_interval_seconds <= 300,
              "Poll interval too long - max 5 minutes");
static_assert(max_cycles > 0, "Must run at least 1 cycle");
static_assert(max_cycles <= 1440,
              "Too many cycles - max 1440 (24 hours at 1 min intervals)");
static_assert(stock_alert_threshold > 0.0,
              "Stock alert threshold must be positive");
static_assert(crypto_alert_threshold > 0.0,
              "Crypto alert threshold must be positive");
static_assert(crypto_alert_threshold >= stock_alert_threshold,
              "Crypto threshold should be >= stock threshold (more volatile)");
static_assert(outlier_threshold > crypto_alert_threshold,
              "Outlier threshold must be higher than alert threshold");
static_assert(outlier_threshold <= 100.0,
              "Outlier threshold too high - max 100%");

} // namespace lft
