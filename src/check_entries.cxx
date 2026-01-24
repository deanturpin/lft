// Phase 2: Entry Checking
// Checks entry signals and executes trades for all watchlist symbols every 15 minutes

#include "lft.h"
#include "defs.h"
#include "strategies.h"
#include <chrono>
#include <format>
#include <map>
#include <nlohmann/json.hpp>
#include <print>
#include <set>
#include <string>
#include <vector>

// Import global tracking state (defined in globals.cxx)
extern std::map<std::string, std::string> position_strategies;
extern std::map<std::string, double> position_peaks;
extern std::map<std::string, std::chrono::system_clock::time_point> position_entry_times;

void check_entries(AlpacaClient &client,
                   const std::map<std::string, bool> &enabled_strategies) {
  // Fetch current positions to avoid duplicate entries
  const auto positions = client.get_positions();
  auto symbols_in_use = std::set<std::string>{};

  for (const auto &pos : positions)
    symbols_in_use.insert(pos.symbol);

  // Build price histories for relative strength (if strategy is enabled)
  auto all_histories = std::map<std::string, PriceHistory>{};
  if (enabled_strategies.contains("relative_strength") and
      enabled_strategies.at("relative_strength")) {
    for (const auto &sym : stocks) {
      if (auto bars = client.get_bars(sym, "15Min", 100)) {
        auto history = PriceHistory{};
        for (const auto &bar : *bars)
          history.add_bar(bar.close, bar.high, bar.low, bar.volume);
        all_histories[sym] = history;
      }
    }
  }

  // Evaluate each watchlist symbol
  for (const auto &symbol : stocks) {
    // Skip if already in position (from API or our tracking)
    if (symbols_in_use.contains(symbol) or position_strategies.contains(symbol))
      continue;

    // Fetch latest bar data and snapshot
    auto bars_opt = client.get_bars(symbol, "15Min", 100);
    auto snapshot_opt = client.get_snapshot(symbol);

    if (not bars_opt or not snapshot_opt)
      continue;

    const auto &bars = *bars_opt;
    const auto &snapshot = *snapshot_opt;

    // Check spread filter (uses industry-standard mid-price calculation)
    const auto spread_bps = Strategies::calculate_spread_bps(snapshot);
    if (spread_bps > max_spread_bps_stocks) {
      std::println("  {} - spread too wide ({:.1f} bps)", symbol, spread_bps);
      continue;
    }

    // Check volume filter (current volume vs 20-bar average)
    if (bars.size() >= 20) {
      auto total_volume = 0.0;
      for (auto i = bars.size() - 20; i < bars.size(); ++i)
        total_volume += bars[i].volume;
      const auto avg_volume = total_volume / 20.0;
      const auto current_volume = bars.back().volume;
      const auto volume_ratio = current_volume / avg_volume;

      if (volume_ratio < min_volume_ratio) {
        std::println("  {} - low volume ({:.1f}% of average)", symbol, volume_ratio * 100.0);
        continue;
      }
    }

    // Convert bars to PriceHistory
    auto history = PriceHistory{};
    for (const auto &bar : bars)
      history.add_bar(bar.close, bar.high, bar.low, bar.volume);

    // Evaluate all strategies
    auto signals = std::vector<StrategySignal>{
        Strategies::evaluate_ma_crossover(history),
        Strategies::evaluate_mean_reversion(history),
        Strategies::evaluate_volatility_breakout(history),
        Strategies::evaluate_relative_strength(history, all_histories),
        Strategies::evaluate_volume_surge(history)
    };

    // Find first enabled signal
    for (const auto &signal : signals) {
      if (not signal.should_buy)
        continue;

      if (not enabled_strategies.contains(signal.strategy_name) or
          not enabled_strategies.at(signal.strategy_name))
        continue;

      std::println("🚨 SIGNAL: {} - {} ({})", symbol, signal.strategy_name, signal.reason);
      std::println("   Placing order for ${:.2f}...", notional_amount);

      // Create unique client_order_id with timestamp
      const auto now = std::chrono::system_clock::now();
      const auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()).count();
      const auto client_order_id = std::format("{}_{}_{}|tp:{:.1f}|sl:-{:.1f}|ts:{:.1f}",
          symbol, signal.strategy_name, timestamp_ms,
          take_profit_pct * 100.0, stop_loss_pct * 100.0, trailing_stop_pct * 100.0);

      auto order = client.place_order(symbol, "buy", notional_amount, client_order_id);
      if (order) {
        // Parse order response to verify status
        auto order_json = nlohmann::json::parse(order.value(), nullptr, false);
        if (not order_json.is_discarded()) {
          const auto order_id = order_json.value("id", "unknown");
          const auto status = order_json.value("status", "unknown");
          const auto side = order_json.value("side", "unknown");
          const auto notional_str = order_json.value("notional", "0");

          std::println("✅ Order placed: ID={} status={} side={} notional=${}",
                       order_id, status, side, notional_str);

          // Only count as executed if order is accepted
          if (status == "accepted" or status == "pending_new" or status == "filled") {
            // Track the position immediately
            position_strategies[symbol] = signal.strategy_name;
            position_entry_times[symbol] = now;
            symbols_in_use.insert(symbol);  // Prevent duplicate orders in same evaluation cycle
          } else {
            std::println("⚠️  Order not accepted: status={}", status);
          }
        } else {
          std::println("❌ Failed to parse order response");
        }
      } else {
        std::println("❌ Order failed: {}", symbol);
      }

      break;  // Only one strategy per symbol
    }
  }
}
