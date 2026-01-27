// Common utilities for LFT trading system
// Shared state, data fetching, and timing functions

#include "lft.h"
#include "defs.h"
#include "strategies.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <map>
#include <print>
#include <set>
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════
// DATA FETCHING AND ASSESSMENT
// ═══════════════════════════════════════════════════════════════════════

std::vector<Snapshot> fetch_snapshots(AlpacaClient &client) {
  auto snapshots = std::vector<Snapshot>{};

  for (const auto &symbol : stocks) {
    if (auto snapshot = client.get_snapshot(symbol))
      snapshots.push_back(*snapshot);
  }

  return snapshots;
}

MarketAssessment
assess_market_conditions(AlpacaClient &client,
                         const std::vector<Snapshot> &snapshots) {
  if (snapshots.empty())
    return {"⚠️  No snapshot data available", false};

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

      const auto daily_change_pct =
          snap.prev_daily_bar_close > 0.0
              ? ((snap.latest_trade_price - snap.prev_daily_bar_close) /
                 snap.prev_daily_bar_close) *
                    100.0
              : 0.0;

      // Fetch recent 1-minute bars for sparkline (1 day = enough for 10 recent
      // bars)
      auto sparkline = std::string{};
      if (auto all_bars = client.get_bars(snap.symbol, "1Min", 1)) {
        // Debug: Log bar count for first symbol only to avoid spam
        static auto first_log = true;
        if (first_log) {
          std::println("  [DEBUG] Fetched {} 1-min bars for {}",
                       all_bars->size(), snap.symbol);
          first_log = false;
        }

        if (all_bars->size() >= 2) {
          // Take only the last 10 bars (or fewer if less available)
          constexpr auto max_sparkline_bars = 10uz;
          const auto start_idx = all_bars->size() > max_sparkline_bars
                                     ? all_bars->size() - max_sparkline_bars
                                     : 0uz;

          // Find min/max close prices for normalization (only in the last N
          // bars)
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
              const auto idx =
                  std::min(static_cast<std::size_t>(normalized * 7.99), 7uz);
              sparkline += sparks[idx];
            } else {
              sparkline += "▄"; // Flat if no movement
            }
          }
        } else {
          sparkline = "▄▄▄"; // Not enough data
        }
      } else {
        sparkline = "▄▄▄"; // Fetch failed
      }

      symbols.push_back({snap.symbol, spread_bps, snap.latest_trade_price,
                         daily_change_pct, sparkline});
    }
  }

  if (count == 0)
    return {"⚠️  No valid quote data", false};

  // Sort by spread (narrowest first)
  std::ranges::sort(symbols, [](const auto &a, const auto &b) {
    return a.spread_bps < b.spread_bps;
  });

  const auto avg_spread_bps = total_spread_bps / static_cast<double>(count);
  const auto tradeable_count =
      std::ranges::count_if(symbols, [](const auto &s) {
        return s.spread_bps <= max_spread_bps_stocks;
      });

  // Calculate market breadth (advancing vs declining)
  const auto advancing = std::ranges::count_if(symbols, [](const auto &s) {
    return s.daily_change_pct > 0.0;
  });
  const auto declining = std::ranges::count_if(symbols, [](const auto &s) {
    return s.daily_change_pct < 0.0;
  });

  // Build summary with table
  const auto emoji = tradeable_count > 0 ? "✅" : "❌";
  auto summary =
      std::format("{} {} of {} symbols tradeable (avg spread: {:.1f} bps) | Breadth: {}/{} advancing\n",
                  emoji, tradeable_count, count, avg_spread_bps, advancing, count);

  // Show table header
  summary += "\n  Symbol   Price    Change  Trend  Spread  Status\n";
  summary += "  ───────────────────────────────────────────────────\n";

  // Show all symbols (or top 15 if too many)
  const auto display_count = std::min(15uz, symbols.size());
  for (auto i = 0uz; i < display_count; ++i) {
    const auto &s = symbols[i];
    const auto status = s.spread_bps <= max_spread_bps_stocks ? "✓" : "✗";

    summary += std::format("  {:7} ${:7.2f}  {:>6.2f}%  {}  {:>5.0f}bp  {}\n",
                           s.symbol, s.price, s.daily_change_pct, s.sparkline,
                           s.spread_bps, status);
  }

  if (symbols.size() > display_count)
    summary +=
        std::format("  ... {} more symbols\n", symbols.size() - display_count);

  return {summary, tradeable_count > 0};
}

std::map<std::string, std::vector<Bar>> fetch_bars(AlpacaClient &client) {
  auto all_bars = std::map<std::string, std::vector<Bar>>{};

  std::println("  Fetching {} days of 15-min bars for {} symbols...",
               calibration_days, stocks.size());

  auto fetched = 0uz;
  for (const auto &symbol : stocks) {
    if (auto bars = client.get_bars(symbol, "15Min", calibration_days)) {
      all_bars[symbol] = *bars;
      ++fetched;
      std::println("    {}/{}: {} ({} bars)", fetched, stocks.size(), symbol,
                   bars->size());
    }
  }

  return all_bars;
}

// ═══════════════════════════════════════════════════════════════════════
// TIMING HELPERS
// ═══════════════════════════════════════════════════════════════════════

std::chrono::system_clock::time_point
next_whole_hour(std::chrono::system_clock::time_point now) {
  const auto now_t = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&now_t);
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_hour += 1;
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::chrono::system_clock::time_point
next_15_minute_bar(std::chrono::system_clock::time_point now) {
  const auto now_t = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&now_t);

  // Round up to next 15-minute boundary (:00, :15, :30, :45)
  const auto current_min = tm.tm_min;
  const auto next_boundary = ((current_min / 15) + 1) * 15;

  if (next_boundary >= 60) {
    tm.tm_hour += 1;
    tm.tm_min = 0;
  } else {
    tm.tm_min = next_boundary;
  }

  tm.tm_sec = 0;
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::chrono::system_clock::time_point
next_minute_at_35_seconds(std::chrono::system_clock::time_point now) {
  const auto now_t = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&now_t);
  tm.tm_sec = 35;
  tm.tm_min += 1;
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::chrono::system_clock::time_point
eod_cutoff_time(std::chrono::system_clock::time_point now) {
  using namespace std::chrono;

  // Convert current time to Eastern Time
  const auto et_now = zoned_time{"America/New_York", now};

  // Get the date in ET and construct 3:50 PM ET
  const auto et_local = et_now.get_local_time();
  const auto et_date = floor<days>(et_local);
  const auto eod_time_et = et_date + 15h + 50min; // 3:50 PM ET

  // Convert back to system_clock::time_point (UTC)
  return zoned_time{"America/New_York", eod_time_et}.get_sys_time();
}

std::chrono::system_clock::time_point
session_start_time(std::chrono::system_clock::time_point now) {
  using namespace std::chrono;

  // Convert current time to Eastern Time
  const auto et_now = zoned_time{"America/New_York", now};

  // Get the date in ET and construct 10:00 AM ET (30 min after market open)
  const auto et_local = et_now.get_local_time();
  const auto et_date = floor<days>(et_local);
  const auto start_time_et = et_date + 10h; // 10:00 AM ET

  // Convert back to system_clock::time_point (UTC)
  return zoned_time{"America/New_York", start_time_et}.get_sys_time();
}

// Check market hours (calculate manually, don't trust Alpaca's is_open field)
bool is_market_hours(std::chrono::system_clock::time_point now) {
  using namespace std::chrono;

  // Convert to Eastern Time
  const auto et_now = zoned_time{"America/New_York", now};
  const auto et_local = et_now.get_local_time();

  // Extract weekday from local time
  const auto et_days = floor<days>(et_local);
  const auto weekday = year_month_weekday{et_days}.weekday();

  // Check if weekend (Saturday or Sunday)
  if (weekday == Saturday or weekday == Sunday)
    return false;

  // Extract time of day in ET
  const auto time_since_midnight = et_local - et_days;
  constexpr auto market_open = 9h + 30min; // 9:30 AM ET
  constexpr auto market_close = 16h;       // 4:00 PM ET

  return (time_since_midnight >= market_open and time_since_midnight < market_close);
}
