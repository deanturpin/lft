// Market Summary Phase
// Displays current market conditions with sparklines showing recent price trends

#include "lft.h"
#include "defs.h"
#include <algorithm>
#include <format>
#include <print>
#include <string>
#include <vector>

namespace lft {

void display_market_summary(AlpacaClient &client) {
  std::println("\n📊 Market Summary:");

  // Fetch latest snapshots for all watchlist symbols
  auto snapshots = fetch_snapshots(client);

  if (snapshots.empty()) {
    std::println("  ⚠️  No snapshot data available");
    return;
  }

  // Build table of symbols sorted by spread (best first)
  struct SymbolInfo {
    std::string symbol;
    double spread_bps;
    double price;
    double daily_change_pct;
    std::string sparkline;
  };

  auto symbols = std::vector<SymbolInfo>{};
  auto total_spread_bps = 0.0;
  auto count = 0uz;

  // Sparkline characters (from lowest to highest)
  constexpr auto sparks = std::array{"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

  for (const auto &snap : snapshots) {
    const auto bid = snap.latest_quote_bid;
    const auto ask = snap.latest_quote_ask;

    if (bid > 0.0 and ask > 0.0) {
      const auto spread_bps = ((ask - bid) / bid) * 10000.0;
      total_spread_bps += spread_bps;
      ++count;

      const auto daily_change_pct = snap.prev_daily_bar_close > 0.0
        ? ((snap.latest_trade_price - snap.prev_daily_bar_close) / snap.prev_daily_bar_close) * 100.0
        : 0.0;

      // Fetch last 10 1-minute bars for sparkline
      auto sparkline = std::string{};
      if (auto all_bars = client.get_bars(snap.symbol, "1Min", 1)) {
        if (all_bars->size() >= 2) {
          // Take only the last 10 bars (or fewer if less available)
          constexpr auto max_sparkline_bars = 10uz;
          const auto start_idx = all_bars->size() > max_sparkline_bars
            ? all_bars->size() - max_sparkline_bars
            : 0uz;

          // Find min/max close prices for normalization (only in the last N bars)
          auto min_price = (*all_bars)[start_idx].close;
          auto max_price = (*all_bars)[start_idx].close;
          for (auto i = start_idx; i < all_bars->size(); ++i) {
            min_price = std::min(min_price, (*all_bars)[i].close);
            max_price = std::max(max_price, (*all_bars)[i].close);
          }

          const auto range = max_price - min_price;

          // Generate sparkline from the last N bar closes
          for (auto i = start_idx; i < all_bars->size(); ++i) {
            const auto &bar = (*all_bars)[i];
            if (range > 0.0) {
              const auto normalized = (bar.close - min_price) / range;
              const auto idx = std::min(static_cast<std::size_t>(normalized * 7.99), 7uz);
              sparkline += sparks[idx];
            } else {
              sparkline += "▄";  // Flat if no movement
            }
          }
        } else {
          sparkline = "---";  // Not enough data
        }
      } else {
        sparkline = "---";  // Fetch failed
      }

      symbols.push_back({snap.symbol, spread_bps, snap.latest_trade_price, daily_change_pct, sparkline});
    }
  }

  if (count == 0) {
    std::println("  ⚠️  No valid quote data");
    return;
  }

  // Sort by spread (narrowest first)
  std::ranges::sort(symbols, [](const auto &a, const auto &b) {
    return a.spread_bps < b.spread_bps;
  });

  const auto avg_spread_bps = total_spread_bps / static_cast<double>(count);
  const auto tradeable_count = std::ranges::count_if(symbols, [](const auto &s) {
    return s.spread_bps <= max_spread_bps_stocks;
  });

  // Build summary header
  const auto emoji = tradeable_count > 0 ? "✅" : "❌";
  std::println("  {} {} of {} symbols tradeable (avg spread: {:.1f} bps)",
               emoji, tradeable_count, count, avg_spread_bps);

  // Show table header
  std::println("\n  Symbol   Price    Change  Trend       Spread  Status");
  std::println("  ──────────────────────────────────────────────────────");

  // Show all symbols (or top 15 if too many)
  const auto display_count = std::min(15uz, symbols.size());
  for (auto i = 0uz; i < display_count; ++i) {
    const auto &s = symbols[i];
    const auto status = s.spread_bps <= max_spread_bps_stocks ? "✓" : "✗";

    std::println("  {:7} ${:7.2f}  {:>6.2f}%  {:10}  {:>5.0f}bp  {}",
                 s.symbol, s.price, s.daily_change_pct, s.sparkline, s.spread_bps, status);
  }

  if (symbols.size() > display_count)
    std::println("  ... {} more symbols", symbols.size() - display_count);
}

} // namespace lft
