#pragma once

#include <cmath>

namespace lft {

// Trading parameters
constexpr auto notional_amount = 100.0;  // Dollar amount per trade
constexpr auto dip_threshold = -2.0;     // Percentage drop to trigger dip strategy (negative)
constexpr auto max_hold_minutes = 120;   // Force exit after 2 hours
constexpr auto calibration_days = 30;    // Use last 30 days for strategy calibration

// Exit parameters (defined in exit_logic_tests.h but used here for reference)
// - take_profit_pct = 2%
// - stop_loss_pct = 2%
// - trailing_stop_pct = 1%

// Timing parameters
constexpr auto poll_interval_seconds = 65;  // Sleep between cycles (60s bar + 5s buffer)
constexpr auto max_cycles = 60;              // Run for 60 minutes then re-calibrate

// Alert thresholds
constexpr auto stock_alert_threshold = 2.0;    // Standard alert at 2% move
constexpr auto crypto_alert_threshold = 5.0;   // Crypto is more volatile
constexpr auto outlier_threshold = 20.0;       // Extreme move requiring attention

// Alert helper functions
constexpr bool is_alert(double change_pct, bool is_crypto) {
    auto threshold = is_crypto ? crypto_alert_threshold : stock_alert_threshold;
    return std::abs(change_pct) >= threshold;
}

constexpr bool is_outlier(double change_pct) {
    return std::abs(change_pct) >= outlier_threshold;
}

// Compile-time safety checks
static_assert(notional_amount > 0.0, "Trade size must be positive");
static_assert(notional_amount >= 1.0, "Trade size too small - minimum $1");
static_assert(notional_amount <= 100000.0, "Trade size dangerously high - max $100k per trade");
static_assert(dip_threshold < 0.0, "Dip threshold must be negative (it's a drop)");
static_assert(dip_threshold >= -50.0, "Dip threshold too extreme - max -50%");
static_assert(max_hold_minutes > 0, "Max hold time must be positive");
static_assert(max_hold_minutes <= 1440, "Max hold time too long - max 24 hours (1440 min)");
static_assert(calibration_days > 0, "Calibration period must be positive");
static_assert(calibration_days >= 7, "Calibration period too short - minimum 7 days");
static_assert(calibration_days <= 365, "Calibration period too long - max 1 year");
static_assert(poll_interval_seconds >= 60, "Poll interval too short - minimum 60s for 1Min bars");
static_assert(poll_interval_seconds <= 300, "Poll interval too long - max 5 minutes");
static_assert(max_cycles > 0, "Must run at least 1 cycle");
static_assert(max_cycles <= 1440, "Too many cycles - max 1440 (24 hours at 1 min intervals)");
static_assert(stock_alert_threshold > 0.0, "Stock alert threshold must be positive");
static_assert(crypto_alert_threshold > 0.0, "Crypto alert threshold must be positive");
static_assert(crypto_alert_threshold >= stock_alert_threshold, "Crypto threshold should be >= stock threshold (more volatile)");
static_assert(outlier_threshold > crypto_alert_threshold, "Outlier threshold must be higher than alert threshold");
static_assert(outlier_threshold <= 100.0, "Outlier threshold too high - max 100%");

} // namespace lft
