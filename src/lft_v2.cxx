// LFT - Low Frequency Trader
// Single-threaded event loop architecture
//
// SYSTEM OVERVIEW:
// 1. Calibrate strategies on 30 days of historic data (runs once per session)
// 2. Entry logic: Evaluate all symbols for entry signals (every 1 minute at :35)
// 3. Exit logic: Check positions for TP/SL/trailing stop (every 10 seconds)
// 4. EOD liquidation: Close all equity positions at 3:55 PM ET
// 5. Session ends after 60 minutes, then restart for fresh calibration
//
// STATE MANAGEMENT:
// - API is single source of truth (no local state files)
// - Position tracking: Query /v2/positions
// - Cooldown tracking: Query recent closed orders from /v2/orders
// - Strategy parameters: Encoded in client_order_id field
//
// TIMING:
// - Calibration: Once at startup (~30 seconds)
// - Entry cycle: Every 60 seconds, aligned to :35 past the minute
// - Exit cycle: Every 10 seconds
// - Session duration: 60 minutes (then restart)
// - Market hours: 9:30 AM - 4:00 PM ET
// - EOD cutoff: 3:55 PM ET (liquidate all equity positions)

#include "alpaca_client.h"
#include "defs.h"
#include "strategies.h"
#include <chrono>
#include <format>
#include <print>

using namespace std::chrono_literals;

// Forward declarations for phase implementations
namespace lft {

// Phase 1: Calibrate strategies on historic data
// Returns: Map of strategy name -> enabled status
auto calibrate_strategies(AlpacaClient &) -> std::map<std::string, bool>;

// Phase 2: Evaluate entry signals for all symbols
// Called every 1 minute at :35 past the minute
auto evaluate_entry_signals(AlpacaClient &,
                            const std::map<std::string, bool> &enabled_strategies,
                            std::chrono::system_clock::time_point now) -> void;

// Phase 3: Check exit conditions for all open positions
// Called every 10 seconds
auto check_exit_conditions(AlpacaClient &,
                           std::chrono::system_clock::time_point now) -> void;

// Phase 4: Emergency liquidation of all equity positions
// Called once at 3:55 PM ET
auto liquidate_all_positions(AlpacaClient &) -> void;

// Helpers for timing
auto calculate_next_minute_at_35_seconds() -> std::chrono::system_clock::time_point;
auto calculate_session_end(std::chrono::system_clock::time_point start)
    -> std::chrono::system_clock::time_point;
auto is_eod_cutoff(std::chrono::system_clock::time_point now) -> bool;
auto is_market_hours(std::chrono::system_clock::time_point now) -> bool;

} // namespace lft

int main() {
  std::println("🚀 LFT - Low Frequency Trader");
  std::println("Starting session at {:%Y-%m-%d %H:%M:%S}\n",
               std::chrono::system_clock::now());

  // Initialize API client
  auto client = lft::AlpacaClient{};

  // ═══════════════════════════════════════════════════════════════════════
  // PHASE 1: CALIBRATION (runs once at session start)
  // ═══════════════════════════════════════════════════════════════════════
  //
  // - Fetches 30 days of historic bars for all symbols
  // - Runs backtest for each strategy with realistic spread simulation
  // - Only enables strategies that show positive P&L
  // - Takes ~30 seconds to complete
  //
  // OUTPUT: Map of strategy name -> enabled (true/false)
  //
  std::println("🎯 PHASE 1: Calibrating strategies...");
  auto session_start = std::chrono::system_clock::now();
  auto enabled_strategies = lft::calibrate_strategies(client);

  // Display calibration results
  std::println("\n📊 Calibration complete:");
  for (const auto &[strategy, enabled] : enabled_strategies)
    std::println("  {} {}", enabled ? "✅" : "❌", strategy);
  std::println("");

  // Calculate when this session should end (60 minutes from now)
  auto session_end = lft::calculate_session_end(session_start);

  // ═══════════════════════════════════════════════════════════════════════
  // MAIN EVENT LOOP (runs until session end or EOD cutoff)
  // ═══════════════════════════════════════════════════════════════════════
  //
  // Serial execution order each cycle:
  // 1. Check if it's time for entry logic (every 1 minute at :35)
  // 2. Check if it's time for exit logic (every 10 seconds)
  // 3. Check if we've hit EOD cutoff (3:55 PM ET)
  // 4. Check if session has ended (60 minutes elapsed)
  // 5. Sleep briefly to avoid tight loop
  //
  // WHY SERIAL?
  // - Both entry and exit call the same API client
  // - Both potentially place orders that affect each other
  // - Simpler than mutexes, no race conditions
  // - Entry/exit complete in <1 second, no parallelism needed
  //

  auto next_entry_time = lft::calculate_next_minute_at_35_seconds();
  auto next_exit_time = std::chrono::system_clock::now() + 10s;
  auto eod_liquidation_done = false;

  std::println("🔄 Entering main event loop...");
  std::println("   Session will end at {:%H:%M:%S}\n",
               session_end);

  while (std::chrono::system_clock::now() < session_end) {
    auto now = std::chrono::system_clock::now();

    // ─────────────────────────────────────────────────────────────────────
    // PHASE 2: ENTRY LOGIC (every 1 minute at :35 past the minute)
    // ─────────────────────────────────────────────────────────────────────
    //
    // Only runs during market hours (9:30 AM - 4:00 PM ET)
    // Skips if we're in EOD liquidation phase
    //
    // For each symbol in watchlist:
    // - Check if already in position (skip if so)
    // - Check if in cooldown period (skip if so)
    // - Fetch latest bar and price history
    // - Evaluate all enabled strategies
    // - Place order if any strategy signals entry
    //
    if (now >= next_entry_time and lft::is_market_hours(now) and
        not eod_liquidation_done) {
      std::println("📥 PHASE 2: Evaluating entry signals at {:%H:%M:%S}", now);
      lft::evaluate_entry_signals(client, enabled_strategies, now);
      next_entry_time = lft::calculate_next_minute_at_35_seconds();
    }

    // ─────────────────────────────────────────────────────────────────────
    // PHASE 3: EXIT LOGIC (every 10 seconds)
    // ─────────────────────────────────────────────────────────────────────
    //
    // Runs during all market hours (including before EOD cutoff)
    //
    // For each open position:
    // - Fetch current price
    // - Check take profit target
    // - Check stop loss
    // - Check trailing stop
    // - Place sell order if any condition met
    //
    if (now >= next_exit_time and lft::is_market_hours(now)) {
      std::println("📤 PHASE 3: Checking exit conditions at {:%H:%M:%S}", now);
      lft::check_exit_conditions(client, now);
      next_exit_time = now + 10s;
    }

    // ─────────────────────────────────────────────────────────────────────
    // PHASE 4: EOD LIQUIDATION (once at 3:55 PM ET)
    // ─────────────────────────────────────────────────────────────────────
    //
    // Emergency close of all equity positions to avoid overnight risk
    // Only affects stocks/ETFs, not crypto (crypto trades 24/7)
    //
    if (lft::is_eod_cutoff(now) and not eod_liquidation_done) {
      std::println("🚨 PHASE 4: EOD cutoff reached, liquidating all positions");
      lft::liquidate_all_positions(client);
      eod_liquidation_done = true;
    }

    // Sleep briefly to avoid tight loop (1 second resolution is fine)
    std::this_thread::sleep_for(1s);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // SESSION END
  // ═══════════════════════════════════════════════════════════════════════
  //
  // After 60 minutes, exit cleanly and let systemd/supervisor restart us
  // This ensures:
  // - Fresh calibration with latest data
  // - No state accumulation bugs
  // - Clean recovery from any API issues
  //
  std::println("\n✅ Session complete at {:%Y-%m-%d %H:%M:%S}",
               std::chrono::system_clock::now());
  std::println("   Exiting for fresh restart...");

  return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// TIMING HELPER IMPLEMENTATIONS
// ═══════════════════════════════════════════════════════════════════════════

namespace lft {

// Calculate next :35 past the minute
// Alpaca recalculates bars at :30 to include late trades, so :35 ensures
// complete data
auto calculate_next_minute_at_35_seconds()
    -> std::chrono::system_clock::time_point {
  auto now = std::chrono::system_clock::now();
  auto now_t = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&now_t);
  tm.tm_sec = 35;
  tm.tm_min += 1; // Next minute
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// Calculate when this session should end (60 minutes from start)
auto calculate_session_end(std::chrono::system_clock::time_point start)
    -> std::chrono::system_clock::time_point {
  return start + std::chrono::minutes{lft::max_cycles};
}

// Check if we've hit EOD cutoff (3:55 PM ET)
auto is_eod_cutoff(std::chrono::system_clock::time_point now) -> bool {
  auto now_t = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&now_t);
  return tm.tm_hour >= 15 and tm.tm_min >= 55;
}

// Check if market is open (9:30 AM - 4:00 PM ET)
// TODO: Add proper market calendar check via Alpaca API
auto is_market_hours(std::chrono::system_clock::time_point now) -> bool {
  auto now_t = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&now_t);

  // Check day of week (0 = Sunday, 6 = Saturday)
  if (tm.tm_wday == 0 or tm.tm_wday == 6)
    return false;

  // Check time (9:30 AM - 4:00 PM)
  auto hour = tm.tm_hour;
  auto min = tm.tm_min;

  if (hour < 9 or hour >= 16)
    return false;
  if (hour == 9 and min < 30)
    return false;

  return true;
}

} // namespace lft
