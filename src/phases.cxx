// Phase implementations for LFT trading system

#include "lft.h"
#include "defs.h"
#include "strategies.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <print>
#include <set>

namespace lft {

// Exit parameters (from exit_logic_tests.h concept)
constexpr auto take_profit_pct = 0.02;    // 2%
constexpr auto stop_loss_pct = 0.05;      // 5%
constexpr auto trailing_stop_pct = 0.30;  // 30%
constexpr auto starting_capital = 100000.0;
constexpr auto notional_per_trade = 1000.0;

// Compile-time validation of exit parameters
static_assert(take_profit_pct > 0.0, "Take profit must be positive");
static_assert(take_profit_pct >= 0.01, "Take profit too small - min 1%");
static_assert(take_profit_pct <= 0.10, "Take profit too large - max 10%");
static_assert(stop_loss_pct > 0.0, "Stop loss must be positive");
static_assert(stop_loss_pct >= 0.01, "Stop loss too small - min 1%");
static_assert(stop_loss_pct <= 0.20, "Stop loss too large - max 20%");
static_assert(stop_loss_pct >= take_profit_pct, "Stop loss should be >= take profit (risk management)");
static_assert(trailing_stop_pct > 0.0, "Trailing stop must be positive");
static_assert(trailing_stop_pct >= 0.05, "Trailing stop too tight - min 5%");
static_assert(trailing_stop_pct <= 0.50, "Trailing stop too loose - max 50%");
static_assert(trailing_stop_pct >= take_profit_pct, "Trailing stop should be >= take profit");
static_assert(starting_capital > 0.0, "Starting capital must be positive");
static_assert(starting_capital >= 10000.0, "Starting capital too low - min $10k");
static_assert(starting_capital <= 10000000.0, "Starting capital dangerously high - max $10M");
static_assert(notional_per_trade > 0.0, "Trade size must be positive");
static_assert(notional_per_trade >= 100.0, "Trade size too small - min $100");
static_assert(notional_per_trade <= starting_capital / 10.0, "Trade size too large - max 10% of capital");
static_assert(notional_per_trade <= 10000.0, "Trade size dangerously high - max $10k per trade");

// Constexpr helper functions for compile-time testing
namespace {
// Calculate P&L percentage
constexpr double calculate_pl_pct(double entry, double current) {
  return (current - entry) / entry;
}

// Check if price hits take profit
constexpr bool hits_take_profit(double entry, double current, double tp_pct) {
  return calculate_pl_pct(entry, current) >= tp_pct;
}

// Check if price hits stop loss
constexpr bool hits_stop_loss(double entry, double current, double sl_pct) {
  return calculate_pl_pct(entry, current) <= -sl_pct;
}

// Check if price hits trailing stop
constexpr bool hits_trailing_stop(double peak, double current, double ts_pct) {
  return current < peak * (1.0 - ts_pct);
}

// Calculate trailing stop price from peak
constexpr double trailing_stop_price(double peak, double ts_pct) {
  return peak * (1.0 - ts_pct);
}
} // namespace

// Compile-time tests for helper functions
static_assert(calculate_pl_pct(100.0, 102.0) == 0.02, "2% gain");
static_assert(calculate_pl_pct(100.0, 98.0) == -0.02, "-2% loss");
static_assert(calculate_pl_pct(100.0, 100.0) == 0.0, "No change");
static_assert(hits_take_profit(100.0, 102.0, 0.02), "TP hit at +2%");
static_assert(not hits_take_profit(100.0, 101.9, 0.02), "TP not hit at +1.9%");
static_assert(hits_stop_loss(100.0, 95.0, 0.05), "SL hit at -5%");
static_assert(not hits_stop_loss(100.0, 95.1, 0.05), "SL not hit at -4.9%");
static_assert(hits_trailing_stop(105.0, 73.4, 0.30), "Trailing stop hit (30% from peak)");
static_assert(not hits_trailing_stop(105.0, 73.6, 0.30), "Trailing stop not hit");
static_assert(trailing_stop_price(100.0, 0.30) == 70.0, "30% trailing stop = 70.0");
static_assert(trailing_stop_price(105.0, 0.30) == 73.5, "30% trailing stop from 105");

// Persistent position tracking state (across function calls)
namespace {
auto position_strategies = std::map<std::string, std::string>{};  // symbol -> strategy name
auto position_peaks = std::map<std::string, double>{};            // symbol -> peak price for trailing stop
auto position_entry_times = std::map<std::string, std::chrono::system_clock::time_point>{};
auto symbol_cooldowns = std::map<std::string, std::chrono::system_clock::time_point>{}; // symbol -> cooldown until
} // namespace

// ═══════════════════════════════════════════════════════════════════════
// DATA FETCHING AND ASSESSMENT
// ═══════════════════════════════════════════════════════════════════════

std::vector<Snapshot> fetch_snapshots(AlpacaClient &client) {
  constexpr auto max_spread_bps = 50.0; // Maximum acceptable spread
  auto snapshots = std::vector<Snapshot>{};

  for (const auto &symbol : stocks) {
    if (auto snapshot = client.get_snapshot(symbol)) {
      // Calculate spread in basis points
      if (snapshot->latest_quote_bid > 0.0 and snapshot->latest_quote_ask > 0.0) {
        const auto spread = snapshot->latest_quote_ask - snapshot->latest_quote_bid;
        snapshot->spread_bps = (spread / snapshot->latest_quote_bid) * 10000.0;
        snapshot->tradeable = snapshot->spread_bps <= max_spread_bps;
      } else {
        snapshot->spread_bps = 0.0;
        snapshot->tradeable = false; // No valid quote
      }
      snapshots.push_back(*snapshot);
    }
  }

  return snapshots;
}

MarketAssessment assess_market_conditions(const std::vector<Snapshot> &snapshots) {
  if (snapshots.empty())
    return {"⚠️  No market data available", false};

  // Calculate average spread from pre-calculated per-symbol spreads
  auto total_spread_bps = 0.0;
  auto tradeable_count = 0uz;

  for (const auto &snap : snapshots) {
    total_spread_bps += snap.spread_bps;
    if (snap.tradeable)
      ++tradeable_count;
  }

  const auto avg_spread_bps = total_spread_bps / snapshots.size();
  const auto tradeable = tradeable_count > 0; // At least one tradeable symbol

  const auto summary = std::format("📊 Market: {} symbols, {:.1f} bps avg spread, {} tradeable {}",
                             snapshots.size(), avg_spread_bps, tradeable_count,
                             tradeable ? "✅" : "⚠️");

  return {summary, tradeable};
}

// ═══════════════════════════════════════════════════════════════════════
// PHASE 1: CALIBRATION
// ═══════════════════════════════════════════════════════════════════════

std::map<std::string, std::vector<Bar>> fetch_bars(AlpacaClient &client) {
  auto bars = std::map<std::string, std::vector<Bar>>{};

  std::println("  Symbol    Bars");
  std::println("  ────────  ─────");

  for (const auto &symbol : stocks) {
    if (auto symbol_bars = client.get_bars(symbol, "15Min", 30)) {
      bars[symbol] = *symbol_bars;
      std::println("  {:8}  {:5}", symbol, symbol_bars->size());
    } else {
      std::println("  {:8}  ERROR", symbol);
    }
  }

  std::println("");
  return bars;
}

// Run backtest simulation for a single strategy
StrategyStats run_backtest_for_strategy(
    std::string_view strategy_name,
    const std::map<std::string, std::vector<Bar>> &all_bars) {

  auto stats = StrategyStats{};
  auto cash = starting_capital;
  auto positions = std::map<std::string, BacktestPosition>{};

  // Find the maximum number of bars across all symbols
  auto max_bars = 0uz;
  for (const auto &[symbol, bars] : all_bars)
    max_bars = std::max(max_bars, bars.size());

  // Build price histories for all symbols (needed for relative_strength)
  auto all_histories = std::map<std::string, PriceHistory>{};
  for (const auto &[symbol, bars] : all_bars)
    all_histories[symbol] = PriceHistory{};

  // Process bar-by-bar across all symbols simultaneously
  for (auto bar_idx = 0uz; bar_idx < max_bars; ++bar_idx) {

    // First pass: Update all histories for this bar index
    for (const auto &[symbol, bars] : all_bars) {
      if (bar_idx < bars.size()) {
        const auto &bar = bars[bar_idx];
        all_histories[symbol].add_bar(bar.close, bar.high, bar.low, bar.volume);
      }
    }

    // Second pass: Process exits and entries for this bar index
    for (const auto &[symbol, bars] : all_bars) {
      if (bar_idx >= bars.size())
        continue;

      const auto &bar = bars[bar_idx];
      auto &history = all_histories[symbol];

      // Check exit conditions for existing position
      if (positions.contains(symbol)) {
        auto &pos = positions[symbol];

        const auto current_price = bar.close;
        const auto pl_dollars = (current_price - pos.entry_price) * pos.quantity;
        const auto pl_pct = pl_dollars / (pos.entry_price * pos.quantity);

        // Update peak for trailing stop
        if (current_price > pos.peak_price)
          pos.peak_price = current_price;

        // Check exit conditions
        const auto should_exit =
            (pl_pct >= take_profit_pct) or                                    // Take profit
            (pl_pct <= -stop_loss_pct) or                                     // Stop loss
            (current_price < pos.peak_price * (1.0 - trailing_stop_pct));     // Trailing stop

        if (should_exit) {
          // Close position
          cash += current_price * pos.quantity;
          ++stats.trades_closed;

          if (pl_dollars > 0.0) {
            ++stats.profitable_trades;
            stats.total_profit += pl_dollars;
          } else {
            ++stats.losing_trades;
            stats.total_loss += pl_dollars;
          }

          positions.erase(symbol);
        }
      }

      // Check entry signals (only if no position and enough cash)
      if (not positions.contains(symbol) and cash >= notional_per_trade and
          history.prices.size() >= 20) {

        // Evaluate strategy signal
        auto signal = StrategySignal{};
        if (strategy_name == "ma_crossover")
          signal = Strategies::evaluate_ma_crossover(history);
        else if (strategy_name == "mean_reversion")
          signal = Strategies::evaluate_mean_reversion(history);
        else if (strategy_name == "volatility_breakout")
          signal = Strategies::evaluate_volatility_breakout(history);
        else if (strategy_name == "relative_strength")
          signal = Strategies::evaluate_relative_strength(history, all_histories);
        else if (strategy_name == "volume_surge")
          signal = Strategies::evaluate_volume_surge(history);

        ++stats.signals_generated;

        if (signal.should_buy and signal.confidence >= 0.7) {
          // Enter position
          const auto entry_price = bar.close;
          const auto quantity = notional_per_trade / entry_price;

          positions[symbol] = BacktestPosition{
              .symbol = symbol,
              .strategy = std::string(strategy_name),
              .entry_price = entry_price,
              .quantity = quantity,
              .entry_bar_index = bar_idx,
              .peak_price = entry_price,
          };

          cash -= entry_price * quantity;
          ++stats.trades_executed;
        }
      }
    }
  }

  // Close any remaining positions at end of history (mark-to-market)
  for (const auto &[symbol, bars] : all_bars) {
    if (positions.contains(symbol)) {
      const auto &pos = positions[symbol];
      const auto &last_bar = bars.back();
      const auto exit_price = last_bar.close;
      const auto pl_dollars = (exit_price - pos.entry_price) * pos.quantity;

      cash += exit_price * pos.quantity;
      ++stats.trades_closed;

      if (pl_dollars > 0.0) {
        ++stats.profitable_trades;
        stats.total_profit += pl_dollars;
      } else {
        ++stats.losing_trades;
        stats.total_loss += pl_dollars;
      }
    }
  }

  return stats;
}

std::map<std::string, bool> calibrate(const std::map<std::string, std::vector<Bar>> &bars) {
  auto enabled = std::map<std::string, bool>{};
  auto strategy_stats = std::map<std::string, StrategyStats>{};

  // Run backtest for each strategy
  std::println("");

  const auto strategies = std::vector<std::string>{
      "ma_crossover", "mean_reversion", "volatility_breakout",
      "relative_strength", "volume_surge"};

  for (const auto &strategy : strategies) {
    std::println("  🔧 Testing {}...", strategy);

    // Run actual backtest
    auto stats = run_backtest_for_strategy(strategy, bars);
    strategy_stats[strategy] = stats;

    // Enable if profitable AND has sufficient trade history
    enabled[strategy] = (stats.net_profit() > 0.0 and stats.trades_closed >= min_trades_to_enable);
  }

  // Print summary table
  std::println("\n📊 Calibration complete:");
  auto enabled_count = 0uz;
  for (const auto &strategy : strategies) {
    const auto &stats = strategy_stats[strategy];
    const auto is_enabled = enabled[strategy];
    const auto status = is_enabled ? "ENABLED " : "DISABLED";

    std::println("  {:<20} {:>10} P&L=${:>8.2f} WR={:>5.1f}%",
                 strategy, status, stats.net_profit(), stats.win_rate());

    if (is_enabled)
      ++enabled_count;
  }

  std::println("\n  {} of {} strategies enabled for live trading\n",
               enabled_count, strategies.size());

  return enabled;
}

// ═══════════════════════════════════════════════════════════════════════
// PHASE 2: ENTRY EVALUATION
// ═══════════════════════════════════════════════════════════════════════

void evaluate_entries(AlpacaClient &client,
                      const std::map<std::string, bool> &enabled_strategies,
                      std::chrono::system_clock::time_point now) {
  // Fetch current positions to avoid duplicate entries
  const auto positions = client.get_positions();
  auto symbols_in_use = std::set<std::string>{};

  for (const auto &pos : positions)
    symbols_in_use.insert(pos.symbol);

  // Evaluate each watchlist symbol
  for (const auto &symbol : stocks) {
    // Skip if already in position (from API or our tracking)
    if (symbols_in_use.contains(symbol) or position_strategies.contains(symbol))
      continue;

    // Skip if in cooldown period
    if (symbol_cooldowns.contains(symbol) and now < symbol_cooldowns[symbol])
      continue;

    // Fetch latest bar data and snapshot
    auto bars_opt = client.get_bars(symbol, "15Min", 100);
    auto snapshot_opt = client.get_snapshot(symbol);

    if (not bars_opt or not snapshot_opt)
      continue;

    const auto &bars = *bars_opt;
    const auto &snapshot = *snapshot_opt;

    // Check spread filter
    const auto spread_bps = ((snapshot.latest_quote_ask - snapshot.latest_quote_bid) /
                             snapshot.latest_quote_bid) * 10000.0;
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
    auto history = lft::PriceHistory{};
    for (const auto &bar : bars)
      history.add_bar(bar.close, bar.high, bar.low, bar.volume);

    // Evaluate all strategies
    auto signals = std::vector<lft::StrategySignal>{
        lft::Strategies::evaluate_ma_crossover(history),
        lft::Strategies::evaluate_mean_reversion(history),
        lft::Strategies::evaluate_volatility_breakout(history),
        lft::Strategies::evaluate_relative_strength(history, {}),  // TODO: Pass all histories
        lft::Strategies::evaluate_volume_surge(history)
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
      const auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()).count();
      const auto client_order_id = std::format("{}_{}_{}|tp:{:.1f}|sl:-{:.1f}|ts:{:.1f}",
          symbol, signal.strategy_name, timestamp_ms,
          take_profit_pct * 100.0, stop_loss_pct * 100.0, trailing_stop_pct * 100.0);

      if (auto order = client.place_order(symbol, "buy", notional_amount, client_order_id)) {
        std::println("✅ Order placed: {}", symbol);

        // Track the position immediately
        position_strategies[symbol] = signal.strategy_name;
        position_entry_times[symbol] = now;
        symbols_in_use.insert(symbol);  // Prevent duplicate orders in same evaluation cycle
      } else {
        std::println("❌ Order failed: {}", symbol);
      }

      break;  // Only one strategy per symbol
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════
// PHASE 3: EXIT CHECKS
// ═══════════════════════════════════════════════════════════════════════

void check_exits(AlpacaClient &client,
                 std::chrono::system_clock::time_point now) {
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

          // Clean up tracking and set cooldown
          position_strategies.erase(pos.symbol);
          position_peaks.erase(pos.symbol);
          position_entry_times.erase(pos.symbol);
          symbol_cooldowns[pos.symbol] = now + std::chrono::minutes{cooldown_minutes};
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

// ═══════════════════════════════════════════════════════════════════════
// PHASE 4: EOD LIQUIDATION
// ═══════════════════════════════════════════════════════════════════════

void liquidate_all(AlpacaClient &client) {
  const auto positions = client.get_positions();

  if (positions.empty()) {
    std::println("  No positions to liquidate");
    return;
  }

  for (const auto &pos : positions) {
    std::println("  Liquidating {} ({} shares)", pos.symbol, pos.qty);
    client.close_position(pos.symbol);
  }
}

// ═══════════════════════════════════════════════════════════════════════
// TIMING HELPERS
// ═══════════════════════════════════════════════════════════════════════

std::chrono::system_clock::time_point next_whole_hour(std::chrono::system_clock::time_point now) {
  const auto now_t = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&now_t);
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_hour += 1;
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::chrono::system_clock::time_point next_15_minute_bar(std::chrono::system_clock::time_point now) {
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

std::chrono::system_clock::time_point next_minute_at_35_seconds(std::chrono::system_clock::time_point now) {
  const auto now_t = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&now_t);
  tm.tm_sec = 35;
  tm.tm_min += 1;
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::chrono::system_clock::time_point eod_cutoff_time(std::chrono::system_clock::time_point now) {
  const auto now_t = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&now_t);
  tm.tm_hour = 15;  // 3 PM
  tm.tm_min = 55;
  tm.tm_sec = 0;
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

bool is_market_hours(std::chrono::system_clock::time_point now) {
  const auto now_t = std::chrono::system_clock::to_time_t(now);
  const auto tm = *std::localtime(&now_t);

  // Check day of week (0 = Sunday, 6 = Saturday)
  if (tm.tm_wday == 0 or tm.tm_wday == 6)
    return false;

  // Check time (9:30 AM - 4:00 PM ET)
  const auto hour = tm.tm_hour;
  const auto min = tm.tm_min;

  if (hour < 9 or hour >= 16)
    return false;
  if (hour == 9 and min < 30)
    return false;

  return true;
}

} // namespace lft
