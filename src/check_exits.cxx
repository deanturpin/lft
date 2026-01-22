// Phase 3: Exit Checking
// Split into two functions:
// - check_normal_exits: TP/SL/trailing (every 15 minutes, same as entries)
// - check_panic_exits: Panic stop only (every 1 minute, fast reaction)

#include "lft.h"
#include "defs.h"
#include <map>
#include <print>
#include <string>

// Import global tracking state (defined in globals.cxx)
extern std::map<std::string, std::string> position_strategies;
extern std::map<std::string, double> position_peaks;
extern std::map<std::string, std::chrono::system_clock::time_point> position_entry_times;

// Phase 3a: Normal exits (TP, SL, trailing) - checked every 15 minutes
void check_normal_exits(AlpacaClient &client, std::chrono::system_clock::time_point now) {
  std::println("\n📤 Checking normal exits at {:%H:%M:%S}",
               std::chrono::floor<std::chrono::seconds>(now));

  const auto positions = client.get_positions();

  if (positions.empty()) {
    std::println("  No open positions");
    return;
  }

  for (const auto &pos : positions) {
    // Fetch current price
    if (auto snapshot = client.get_snapshot(pos.symbol)) {
      const auto current_price = snapshot->latest_trade_price;
      const auto unrealized_pl = pos.unrealized_pl;
      const auto cost_basis = pos.avg_entry_price * pos.qty;
      const auto pl_pct = (unrealized_pl / cost_basis);

      // Update peak price for trailing stop
      if (not position_peaks.contains(pos.symbol))
        position_peaks[pos.symbol] = current_price;
      else if (current_price > position_peaks[pos.symbol])
        position_peaks[pos.symbol] = current_price;

      // Calculate exit conditions
      const auto peak = position_peaks[pos.symbol];
      const auto trailing_stop_price = peak * (1.0 - trailing_stop_pct);
      const auto trailing_stop_triggered = current_price < trailing_stop_price;

      // Only check normal exits (no panic stop here)
      const auto should_exit = pl_pct >= take_profit_pct or
                               pl_pct <= -stop_loss_pct or
                               trailing_stop_triggered;

      if (should_exit) {
        const auto profit_percent = pl_pct * 100.0;

        auto exit_reason = std::string{};
        if (trailing_stop_triggered)
          exit_reason = "TRAILING STOP";
        else if (unrealized_pl > 0.0)
          exit_reason = "PROFIT TARGET";
        else
          exit_reason = "STOP LOSS";

        std::println("{} {}: {} ${:.2f} ({:+.2f}%)",
                     unrealized_pl > 0.0 ? "💰" : "🛑", exit_reason,
                     pos.symbol, unrealized_pl, profit_percent);
        std::println("   Closing position...");

        if (client.close_position(pos.symbol)) {
          std::println("✅ Position closed: {}", pos.symbol);

          // Clean up tracking (cooldown no longer needed with 15-min entry cycle)
          position_strategies.erase(pos.symbol);
          position_peaks.erase(pos.symbol);
          position_entry_times.erase(pos.symbol);
        } else {
          std::println("❌ Failed to close position: {}", pos.symbol);
        }
      } else {
        // Just log the position status
        const auto profit_percent = pl_pct * 100.0;
        std::println("  {} @ ${:.2f} ({:+.2f}%)", pos.symbol, current_price, profit_percent);
      }
    }
  }
}

// Phase 3b: Panic exits only - checked every 1 minute for fast reaction
void check_panic_exits(AlpacaClient &client, std::chrono::system_clock::time_point) {
  const auto positions = client.get_positions();

  if (positions.empty())
    return;

  for (const auto &pos : positions) {
    // Fetch current price
    if (auto snapshot = client.get_snapshot(pos.symbol)) {
      const auto unrealized_pl = pos.unrealized_pl;
      const auto cost_basis = pos.avg_entry_price * pos.qty;
      const auto pl_pct = (unrealized_pl / cost_basis);

      // Only check panic stop (catastrophic loss)
      const auto panic_stop_triggered = pl_pct <= -panic_stop_loss_pct;

      if (panic_stop_triggered) {
        const auto profit_percent = pl_pct * 100.0;

        std::println("🚨 PANIC STOP: {} ${:.2f} ({:+.2f}%)",
                     pos.symbol, unrealized_pl, profit_percent);
        std::println("   Closing position immediately...");

        if (client.close_position(pos.symbol)) {
          std::println("✅ Position closed: {}", pos.symbol);

          // Clean up tracking
          position_strategies.erase(pos.symbol);
          position_peaks.erase(pos.symbol);
          position_entry_times.erase(pos.symbol);
        } else {
          std::println("❌ Failed to close position: {}", pos.symbol);
        }
      }
    }
  }
}
