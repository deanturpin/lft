#pragma once

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

} // namespace lft
