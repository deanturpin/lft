#include "shared/alpaca_client.h"
#include "shared/strategies.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <print>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

// Include compile-time exit logic tests
// This header defines: operator""_pc, stock_spread, crypto_spread,
// take_profit_pct, stop_loss_pct, trailing_stop_pct
#include "../tests/exit_logic_tests.h"

using namespace std::chrono_literals;

namespace {

// ANSI colour codes
constexpr auto colour_reset = "\033[0m";
constexpr auto colour_green = "\033[32m";
constexpr auto colour_red = "\033[31m";
constexpr auto colour_cyan = "\033[36m";
constexpr auto colour_yellow = "\033[33m";

// Trading parameters
constexpr auto dip_threshold = -2_pc;
constexpr auto notional_amount = 100.0;

// Calibration parameters
constexpr auto calibration_days = 30; // Use last 30 days for better calibration

// Time-based exit parameter (not in test header as it's not tested)
constexpr auto max_hold_minutes = 120; // Force exit after 2 hours

// Position tracking for backtest
struct Position {
  std::string symbol;
  std::string strategy;
  double entry_price{};
  double quantity{};
  std::string entry_time;
  std::size_t entry_bar_index{}; // For time-based exit in backtest
  double peak_price{};
  double take_profit_pct{};
  double stop_loss_pct{};
  double trailing_stop_pct{};
};

struct BacktestStats {
  std::map<std::string, lft::StrategyStats> strategy_stats;
  double cash{100000.0}; // Starting capital
  std::map<std::string, Position> positions;
  int total_trades{};
  int winning_trades{};
  int losing_trades{};
};

void process_bar(const std::string &symbol, const lft::Bar &bar,
                 std::size_t bar_index, lft::PriceHistory &history,
                 const std::map<std::string, lft::PriceHistory> &all_histories,
                 BacktestStats &stats,
                 const std::map<std::string, lft::StrategyConfig> &configs) {

  history.add_price(bar.close);

  auto has_position = stats.positions.contains(symbol);

  if (has_position) {
    auto &pos = stats.positions[symbol];

    // Apply spread: sell at bid (mid - half spread)
    auto is_crypto = symbol.find('/') != std::string::npos;
    auto spread = is_crypto ? crypto_spread : stock_spread;
    auto half_spread = bar.close * (spread / 2.0);
    auto sell_price = bar.close - half_spread;

    auto current_value = pos.quantity * sell_price;
    auto cost_basis = pos.quantity * pos.entry_price;
    auto unrealized_pl = current_value - cost_basis;
    auto pl_pct = (sell_price - pos.entry_price) / pos.entry_price;

    // Update peak price for trailing stop (use mid price)
    if (bar.close > pos.peak_price)
      pos.peak_price = bar.close;

    // Calculate trailing stop trigger (price falls below peak by
    // trailing_stop_pct)
    auto trailing_stop_price = pos.peak_price * (1.0 - pos.trailing_stop_pct);
    auto trailing_stop_triggered = sell_price < trailing_stop_price;

    // Check time-based exit (1 bar = 1 minute for 1Min bars)
    auto bars_held = bar_index - pos.entry_bar_index;
    auto time_exit_triggered = bars_held >= max_hold_minutes;

    // Exit conditions using position-specific parameters
    auto should_exit = pl_pct >= pos.take_profit_pct or
                       pl_pct <= -pos.stop_loss_pct or trailing_stop_triggered or
                       time_exit_triggered;

    if (should_exit) {
      stats.cash += current_value;

      auto &strategy_stat = stats.strategy_stats[pos.strategy];
      ++strategy_stat.trades_closed;

      if (unrealized_pl > 0.0) {
        ++strategy_stat.profitable_trades;
        strategy_stat.total_profit += unrealized_pl;
        ++stats.winning_trades;
      } else {
        ++strategy_stat.losing_trades;
        strategy_stat.total_loss += unrealized_pl;
        ++stats.losing_trades;
      }

      stats.positions.erase(symbol);
    }
  } else {
    // Evaluate entry strategies
    auto signals = std::vector<lft::StrategySignal>{
        // is this history of all assets?
        lft::Strategies::evaluate_dip(history, dip_threshold),
        lft::Strategies::evaluate_ma_crossover(history),
        lft::Strategies::evaluate_mean_reversion(history),
        lft::Strategies::evaluate_volatility_breakout(history),
        lft::Strategies::evaluate_relative_strength(history, all_histories)};

    // Count signals and execute trades ONLY for enabled strategies
    for (const auto &signal : signals) {
      // Only process signals from enabled strategies (during calibration, only
      // one is enabled)
      if (not configs.contains(signal.strategy_name) or
          not configs.at(signal.strategy_name).enabled)
        continue;

      if (signal.should_buy) {
        ++stats.strategy_stats[signal.strategy_name].signals_generated;

        // Execute if we have cash
        if (stats.cash >= notional_amount) {

          // Apply spread: buy at ask (mid + half spread)
          auto is_crypto = symbol.find('/') != std::string::npos;
          auto spread = is_crypto ? crypto_spread : stock_spread;
          auto half_spread = bar.close * (spread / 2.0);
          auto buy_price = bar.close + half_spread;

          auto quantity = notional_amount / buy_price;
          auto actual_cost = quantity * buy_price;

          stats.cash -= actual_cost;

          const auto &config = configs.at(signal.strategy_name);
          stats.positions[symbol] =
              Position{.symbol = symbol,
                       .strategy = signal.strategy_name,
                       .entry_price = buy_price, // Store actual buy price (ask)
                       .quantity = quantity,
                       .entry_time = bar.timestamp,
                       .entry_bar_index = bar_index,
                       .peak_price = bar.close, // Peak tracks mid price
                       .take_profit_pct = config.take_profit_pct,
                       .stop_loss_pct = config.stop_loss_pct,
                       .trailing_stop_pct = config.trailing_stop_pct};

          ++stats.strategy_stats[signal.strategy_name].trades_executed;
          ++stats.total_trades;

          break; // Only one strategy per symbol
        }
      }
    }
  }
}

// Run calibration simulation with pre-fetched data
BacktestStats run_backtest_with_data(
    const std::map<std::string, std::vector<lft::Bar>> &symbol_bars,
    const std::map<std::string, lft::StrategyConfig> &configs) {

  auto stats = BacktestStats{};
  stats.strategy_stats["dip"] = lft::StrategyStats{"dip"};
  stats.strategy_stats["ma_crossover"] = lft::StrategyStats{"ma_crossover"};
  stats.strategy_stats["mean_reversion"] = lft::StrategyStats{"mean_reversion"};
  stats.strategy_stats["volatility_breakout"] =
      lft::StrategyStats{"volatility_breakout"};
  stats.strategy_stats["relative_strength"] =
      lft::StrategyStats{"relative_strength"};

  auto price_histories = std::map<std::string, lft::PriceHistory>{};

  // Find max bars
  auto max_bars = 0uz;
  for (const auto &[sym, bars] : symbol_bars)
    max_bars = std::max(max_bars, bars.size());

  // Simulate
  for (auto i = 0uz; i < max_bars; ++i) {
    for (const auto &[symbol, bars] : symbol_bars) {
      if (i >= bars.size())
        continue;

      const auto &bar = bars[i];
      auto &history = price_histories[symbol];

      process_bar(symbol, bar, i, history, price_histories, stats, configs);
    }
  }

  // Close remaining positions
  for (const auto &[symbol, pos] : stats.positions) {
    if (not symbol_bars.contains(symbol))
      continue;

    const auto &bars = symbol_bars.at(symbol);
    if (bars.empty())
      continue;

    // Apply spread when closing final positions
    auto is_crypto = symbol.find('/') != std::string::npos;
    auto spread = is_crypto ? crypto_spread : stock_spread;
    auto final_mid = bars.back().close;
    auto half_spread = final_mid * (spread / 2.0);
    auto final_sell_price = final_mid - half_spread;

    auto current_value = pos.quantity * final_sell_price;
    auto cost_basis = pos.quantity * pos.entry_price;
    auto unrealized_pl = current_value - cost_basis;

    stats.cash += current_value;

    auto &strategy_stat = stats.strategy_stats[pos.strategy];
    ++strategy_stat.trades_closed;

    if (unrealized_pl > 0.0) {
      ++strategy_stat.profitable_trades;
      strategy_stat.total_profit += unrealized_pl;
      ++stats.winning_trades;
    } else {
      ++strategy_stat.losing_trades;
      strategy_stat.total_loss += unrealized_pl;
      ++stats.losing_trades;
    }
  }
  stats.positions.clear();

  return stats;
}

// Mutex for thread-safe printing during parallel calibration
std::mutex calibration_print_mutex;

// Test strategy with fixed exit parameters
lft::StrategyConfig calibrate_strategy(
    const std::string &strategy_name,
    const std::map<std::string, std::vector<lft::Bar>> &symbol_bars) {

  {
    auto lock = std::scoped_lock{calibration_print_mutex};
    std::println("{}🔧 Testing {}...{}", colour_cyan, strategy_name,
                 colour_reset);
  }

  // Create config with fixed exit parameters
  auto configs = std::map<std::string, lft::StrategyConfig>{};
  configs[strategy_name] =
      lft::StrategyConfig{.name = strategy_name,
                          .enabled = true,
                          .take_profit_pct = take_profit_pct,
                          .stop_loss_pct = stop_loss_pct,
                          .trailing_stop_pct = trailing_stop_pct};

  auto stats = run_backtest_with_data(symbol_bars, configs);
  auto profit = stats.strategy_stats[strategy_name].net_profit();
  auto trades = stats.strategy_stats[strategy_name].trades_closed;
  auto signals = stats.strategy_stats[strategy_name].signals_generated;
  auto win_rate = stats.strategy_stats[strategy_name].win_rate();

  auto config = configs[strategy_name];
  config.net_profit = profit;
  config.win_rate = win_rate;

  {
    auto lock = std::scoped_lock{calibration_print_mutex};
    auto colour = profit > 0.0 ? colour_green : colour_red;
    std::println(
        "{}✓ {} Complete: {} signals, {} trades, ${:.2f} P&L, {:.1f}% WR{}",
        colour, strategy_name, signals, trades, profit, win_rate, colour_reset);
  }

  return config;
}

// Run calibration phase
std::map<std::string, lft::StrategyConfig>
calibrate_all_strategies(lft::AlpacaClient &client,
                         const std::vector<std::string> &stocks,
                         const std::vector<std::string> &crypto) {

  std::println("\n{}🎯 CALIBRATION PHASE{}", colour_cyan, colour_reset);
  std::println("Testing last {} days of data with fixed exit parameters",
               calibration_days);
  std::println("Exit parameters:");
  std::println("  Take Profit: {:.1f}%", take_profit_pct * 100.0);
  std::println("  Stop Loss: {:.1f}%", stop_loss_pct * 100.0);
  std::println("  Trailing Stop: {:.1f}%", trailing_stop_pct * 100.0);
  std::println("  Max Hold Time: {} minutes", max_hold_minutes);

  // Fetch historic data ONCE upfront (huge speedup!)
  std::println("\n{}📥 Fetching historic data{}", colour_yellow, colour_reset);

  // Fetch most recent available data (end at yesterday to avoid "recent SIP
  // data" restriction)
  auto yesterday = std::chrono::system_clock::now() - std::chrono::hours(24);
  auto start_date = yesterday - std::chrono::hours(24 * calibration_days);
  auto start = std::format("{:%Y-%m-%dT%H:%M:%SZ}", start_date);
  auto end = std::format("{:%Y-%m-%dT%H:%M:%SZ}", yesterday);

  auto all_symbols = stocks;
  all_symbols.insert(all_symbols.end(), crypto.begin(), crypto.end());

  auto symbol_bars = std::map<std::string, std::vector<lft::Bar>>{};

  for (const auto &symbol : all_symbols) {
    auto is_crypto = symbol.find('/') != std::string::npos;
    auto bars = is_crypto ? client.get_crypto_bars(symbol, "1Min", start, end)
                          : client.get_bars(symbol, "1Min", start, end);

    if (bars) {
      symbol_bars[symbol] = std::move(*bars);
      std::println("  {} - {} bars", symbol, symbol_bars[symbol].size());
    }
  }

  std::println("{}✓ Data fetched - ready for calibration{}\n", colour_green,
               colour_reset);

  auto strategies =
      std::vector<std::string>{"dip", "ma_crossover", "mean_reversion",
                               "volatility_breakout", "relative_strength"};

  // Calibrate all strategies in parallel using threads
  auto strategy_configs = std::vector<lft::StrategyConfig>(strategies.size());
  auto threads = std::vector<std::thread>{};

  for (auto i = 0uz; i < strategies.size(); ++i) {
    threads.emplace_back([i, &strategies, &strategy_configs, &symbol_bars]() {
      auto config = calibrate_strategy(strategies[i], symbol_bars);
      config.enabled = config.net_profit > 0.0; // Only enable if profitable
      strategy_configs[i] = config;
    });
  }

  // Wait for all calibrations to complete
  for (auto &thread : threads)
    thread.join();

  // Build map from parallel results
  auto configs = std::map<std::string, lft::StrategyConfig>{};
  for (auto i = 0uz; i < strategies.size(); ++i)
    configs[strategies[i]] = strategy_configs[i];

  // Print summary
  std::println("\n{}📊 CALIBRATION RESULTS{}", colour_cyan, colour_reset);
  std::println("{:-<80}", "");

  auto enabled_count = 0;
  for (const auto &[name, config] : configs) {
    auto colour = config.enabled ? colour_green : colour_red;
    auto status = config.enabled ? "ENABLED" : "DISABLED";
    std::println("{}{:<20} {:>10} P&L=${:>8.2f} WR={:>5.1f}%{}", colour, name,
                 status, config.net_profit, config.win_rate, colour_reset);
    if (config.enabled)
      ++enabled_count;
  }

  std::println("\n{} of {} strategies enabled for live trading\n",
               enabled_count, strategies.size());

  return configs;
}

void print_header() {
  std::println("\n{:<10} {:>12} {:>12} {:>12} {:>10} {}", "SYMBOL", "LAST",
               "BID", "ASK", "CHANGE%", "STATUS");
  std::println("{:-<70}", "");
}

void print_snapshot(const std::string &symbol, const lft::Snapshot &snap,
                    lft::PriceHistory &history) {
  constexpr auto alert_threshold = 2_pc;
  auto status = std::string{};
  auto colour = colour_reset;

  history.add_price(snap.latest_trade_price);

  if (history.has_history) {
    if (history.change_percent > 0.0)
      colour = colour_green;
    else if (history.change_percent < 0.0)
      colour = colour_red;

    if (std::abs(history.change_percent) >= alert_threshold)
      status = "🚨 ALERT";
  }

  std::println("{}{:<10} {:>12.2f} {:>12.2f} {:>12.2f} {:>9.2f}% {}{}", colour,
               symbol, snap.latest_trade_price, snap.latest_quote_bid,
               snap.latest_quote_ask, history.change_percent, status,
               colour_reset);
}

void print_strategy_stats(
    const std::map<std::string, lft::StrategyStats> &stats) {
  std::println("\n📊 STRATEGY PERFORMANCE");
  std::println("{:-<110}", "");
  std::println("{:<18} {:>10} {:>10} {:>10} {:>10} {:>12} {:>12} {:>12}",
               "STRATEGY", "SIGNALS", "EXECUTED", "CLOSED", "WINS", "WIN RATE",
               "NET P&L", "AVG P&L");
  std::println("{:-<110}", "");

  for (const auto &[name, stat] : stats) {
    auto colour = stat.net_profit() >= 0.0 ? colour_green : colour_red;
    auto avg_pl =
        stat.trades_closed > 0 ? stat.net_profit() / stat.trades_closed : 0.0;

    std::println(
        "{}{:<18} {:>10} {:>10} {:>10} {:>10} {:>11.1f}% {:>11.2f} {:>11.2f}{}",
        colour, stat.name, stat.signals_generated, stat.trades_executed,
        stat.trades_closed, stat.profitable_trades, stat.win_rate(),
        stat.net_profit(), avg_pl, colour_reset);
  }
  std::println("");
}

// Live trading loop
void run_live_trading(
    lft::AlpacaClient &client, const std::vector<std::string> &stocks,
    const std::vector<std::string> &crypto,
    const std::map<std::string, lft::StrategyConfig> &configs) {

  std::println("{}🚀 LIVE TRADING MODE{}", colour_green, colour_reset);
  std::println("Using calibrated exit parameters per strategy");
  std::println("Running for 1 hour, then will re-calibrate\n");

  auto price_histories = std::map<std::string, lft::PriceHistory>{};
  auto position_strategies = std::map<std::string, std::string>{};
  auto position_configs = std::map<std::string, lft::StrategyConfig>{};
  auto position_peaks = std::map<std::string, double>{};
  auto position_entry_times =
      std::map<std::string, std::chrono::system_clock::time_point>{};
  auto existing_positions = std::set<std::string>{};
  auto strategy_stats = std::map<std::string, lft::StrategyStats>{};

  // Initialise stats for enabled strategies only
  for (const auto &[name, config] : configs) {
    if (config.enabled)
      strategy_stats[name] = lft::StrategyStats{name};
  }

  constexpr auto max_cycles = 60; // Run for 60 minutes (60 cycles)
  auto cycle = 0;

  while (cycle < max_cycles) {
    ++cycle;
    auto now = std::chrono::system_clock::now();
    std::println("\n{:-<70}", "");
    std::println("Tick at {:%Y-%m-%d %H:%M:%S}", now);
    std::println("{:-<70}", "");

    // Fetch and process positions
    auto positions_result = client.get_positions();
    if (positions_result) {
      auto positions_json =
          nlohmann::json::parse(positions_result.value(), nullptr, false);

      if (not positions_json.is_discarded()) {
        existing_positions.clear();
        for (const auto &pos : positions_json)
          existing_positions.insert(pos["symbol"].get<std::string>());

        if (not positions_json.empty()) {
          std::println("\n📊 OPEN POSITIONS");
          std::println("{:-<100}", "");
          std::println("{:<10} {:>10} {:>15} {:>15} {:>15} {:>10} {:<18}",
                       "SYMBOL", "QTY", "ENTRY PRICE", "CURRENT PRICE",
                       "MARKET VALUE", "P&L %", "STRATEGY");
          std::println("{:-<100}", "");

          for (const auto &pos : positions_json) {
            auto symbol = pos["symbol"].get<std::string>();
            auto qty = pos["qty"].get<std::string>();
            auto avg_entry =
                std::stod(pos["avg_entry_price"].get<std::string>());
            auto current = std::stod(pos["current_price"].get<std::string>());
            auto market_value =
                std::stod(pos["market_value"].get<std::string>());
            auto unrealized_plpc =
                std::stod(pos["unrealized_plpc"].get<std::string>()) * 100.0;

            auto colour = unrealized_plpc >= 0.0 ? colour_green : colour_red;
            auto strategy = position_strategies.contains(symbol)
                                ? position_strategies[symbol]
                                : "manual";

            std::println("{}{:<10} {:>10} {:>15.2f} {:>15.2f} {:>15.2f} "
                         "{:>9.2f}% {:<18}{}",
                         colour, symbol, qty, avg_entry, current, market_value,
                         unrealized_plpc, strategy, colour_reset);
          }
          std::println("");

          // Evaluate exits using strategy-specific parameters
          for (const auto &pos : positions_json) {
            auto symbol = pos["symbol"].get<std::string>();
            auto unrealized_pl =
                std::stod(pos["unrealized_pl"].get<std::string>());
            auto cost_basis = std::stod(pos["cost_basis"].get<std::string>());
            auto current_price =
                std::stod(pos["current_price"].get<std::string>());
            auto avg_entry =
                std::stod(pos["avg_entry_price"].get<std::string>());
            auto pl_pct = (current_price - avg_entry) / avg_entry;

            // Skip if no strategy config
            if (not position_configs.contains(symbol))
              continue;

            const auto &config = position_configs[symbol];

            // Update peak price
            if (not position_peaks.contains(symbol))
              position_peaks[symbol] = current_price;
            else if (current_price > position_peaks[symbol])
              position_peaks[symbol] = current_price;

            // Check trailing stop (price falls below peak by trailing_stop_pct)
            auto peak = position_peaks[symbol];
            auto trailing_stop_price = peak * (1.0 - config.trailing_stop_pct);
            auto trailing_stop_triggered = current_price < trailing_stop_price;

            // Check time-based exit (approximate: each cycle is ~1 minute)
            auto time_exit_triggered = false;
            if (position_entry_times.contains(symbol)) {
              auto elapsed_seconds =
                  std::chrono::duration<double>(now - position_entry_times[symbol])
                      .count();
              auto minutes_held = static_cast<long long>(elapsed_seconds / 60.0);
              time_exit_triggered = minutes_held >= max_hold_minutes;
            }

            // Exit using strategy-specific parameters
            auto should_exit = pl_pct >= config.take_profit_pct or
                               pl_pct <= -config.stop_loss_pct or
                               trailing_stop_triggered or time_exit_triggered;

            if (should_exit) {
              auto profit_percent = (unrealized_pl / cost_basis) * 100.0;
              auto strategy = position_strategies[symbol];

              auto exit_reason = std::string{};
              if (time_exit_triggered)
                exit_reason = "TIME LIMIT";
              else if (trailing_stop_triggered)
                exit_reason = "TRAILING STOP";
              else if (unrealized_pl > 0.0)
                exit_reason = "PROFIT TARGET";
              else
                exit_reason = "STOP LOSS";

              std::println("{} {}: {} ${:.2f} ({:.2f}%) from {}",
                           unrealized_pl > 0.0 ? "💰" : "🛑", exit_reason,
                           symbol, unrealized_pl, profit_percent, strategy);
              std::println("   Closing position...");

              auto close_result = client.close_position(symbol);
              if (close_result) {
                std::println("✅ Position closed: {}", symbol);
                existing_positions.erase(symbol);

                if (strategy_stats.contains(strategy)) {
                  ++strategy_stats[strategy].trades_closed;
                  if (unrealized_pl > 0.0) {
                    ++strategy_stats[strategy].profitable_trades;
                    strategy_stats[strategy].total_profit += unrealized_pl;
                  } else {
                    ++strategy_stats[strategy].losing_trades;
                    strategy_stats[strategy].total_loss += unrealized_pl;
                  }
                }

                position_strategies.erase(symbol);
                position_configs.erase(symbol);
                position_peaks.erase(symbol);
                position_entry_times.erase(symbol);
              } else {
                std::println("❌ Failed to close position: {}", symbol);
              }
            }
          }
        }
      }
    }

    // Fetch market data
    auto stock_snapshots = client.get_snapshots(stocks);
    auto crypto_snapshots = client.get_crypto_snapshots(crypto);

    print_header();

    // Process stocks
    if (stock_snapshots) {
      for (const auto &symbol : stocks) {
        if (stock_snapshots->contains(symbol)) {
          auto &snap = stock_snapshots->at(symbol);
          auto &history = price_histories[symbol];

          print_snapshot(symbol, snap, history);

          // Entry logic - only for enabled strategies
          auto signals = std::vector<lft::StrategySignal>{
              lft::Strategies::evaluate_dip(history, dip_threshold),
              lft::Strategies::evaluate_ma_crossover(history),
              lft::Strategies::evaluate_mean_reversion(history),
              lft::Strategies::evaluate_volatility_breakout(history),
              lft::Strategies::evaluate_relative_strength(history,
                                                          price_histories)};

          // Count signals
          for (const auto &signal : signals) {
            if (signal.should_buy and configs.contains(signal.strategy_name) and
                configs.at(signal.strategy_name).enabled)
              ++strategy_stats[signal.strategy_name].signals_generated;
          }

          if (not existing_positions.contains(symbol)) {
            // Execute first enabled signal
            for (const auto &signal : signals) {
              if (signal.should_buy and
                  configs.contains(signal.strategy_name) and
                  configs.at(signal.strategy_name).enabled) {

                std::println("{}🚨 SIGNAL: {} - {}{}", colour_cyan,
                             signal.strategy_name, signal.reason, colour_reset);
                std::println("   Buying ${:.0f} of {}...", notional_amount,
                             symbol);

                auto order = client.place_order(symbol, "buy", notional_amount);
                if (order) {
                  std::println("✅ Order placed");
                  existing_positions.insert(symbol);
                  position_strategies[symbol] = signal.strategy_name;
                  position_configs[symbol] = configs.at(signal.strategy_name);
                  position_entry_times[symbol] = now;
                  ++strategy_stats[signal.strategy_name].trades_executed;
                } else {
                  std::println("❌ Order failed");
                }

                break; // One strategy per symbol
              }
            }
          } else {
            // Check if blocked by existing position
            for (const auto &signal : signals) {
              if (signal.should_buy and
                  configs.contains(signal.strategy_name) and
                  configs.at(signal.strategy_name).enabled) {
                std::println(
                    "{}⏸  BLOCKED: {} signal for {} (position already open){}",
                    colour_yellow, signal.strategy_name, symbol, colour_reset);
                break;
              }
            }
          }
        }
      }
    }

    // Process crypto
    if (crypto_snapshots) {
      for (const auto &symbol : crypto) {
        if (crypto_snapshots->contains(symbol)) {
          auto &snap = crypto_snapshots->at(symbol);
          auto &history = price_histories[symbol];

          print_snapshot(symbol, snap, history);

          // Entry logic - only for enabled strategies
          auto signals = std::vector<lft::StrategySignal>{
              lft::Strategies::evaluate_dip(history, dip_threshold),
              lft::Strategies::evaluate_ma_crossover(history),
              lft::Strategies::evaluate_mean_reversion(history),
              lft::Strategies::evaluate_volatility_breakout(history),
              lft::Strategies::evaluate_relative_strength(history,
                                                          price_histories)};

          // Count signals
          for (const auto &signal : signals) {
            if (signal.should_buy and configs.contains(signal.strategy_name) and
                configs.at(signal.strategy_name).enabled)
              ++strategy_stats[signal.strategy_name].signals_generated;
          }

          if (not existing_positions.contains(symbol)) {
            // Execute first enabled signal
            for (const auto &signal : signals) {
              if (signal.should_buy and
                  configs.contains(signal.strategy_name) and
                  configs.at(signal.strategy_name).enabled) {

                std::println("{}🚨 SIGNAL: {} - {}{}", colour_cyan,
                             signal.strategy_name, signal.reason, colour_reset);
                std::println("   Buying ${:.0f} of {}...", notional_amount,
                             symbol);

                auto order = client.place_order(symbol, "buy", notional_amount);
                if (order) {
                  std::println("✅ Order placed");
                  existing_positions.insert(symbol);
                  position_strategies[symbol] = signal.strategy_name;
                  position_configs[symbol] = configs.at(signal.strategy_name);
                  position_entry_times[symbol] = now;
                  ++strategy_stats[signal.strategy_name].trades_executed;
                } else {
                  std::println("❌ Order failed");
                }

                break; // One strategy per symbol
              }
            }
          } else {
            // Check if blocked by existing position
            for (const auto &signal : signals) {
              if (signal.should_buy and
                  configs.contains(signal.strategy_name) and
                  configs.at(signal.strategy_name).enabled) {
                std::println(
                    "{}⏸  BLOCKED: {} signal for {} (position already open){}",
                    colour_yellow, signal.strategy_name, symbol, colour_reset);
                break;
              }
            }
          }
        }
      }
    }

    // Print stats
    print_strategy_stats(strategy_stats);

    // Calculate cycles remaining
    auto cycles_remaining = max_cycles - cycle;

    std::println(
        "\n⏳ Next update in 60 seconds | {} cycles until re-calibration\n",
        cycles_remaining);
    std::this_thread::sleep_for(60s);
  }
}

} // anonymous namespace

int main() {
  auto client = lft::AlpacaClient{};

  if (not client.is_valid()) {
    std::println("❌ ALPACA_API_KEY and ALPACA_API_SECRET must be set");
    return 1;
  }

  std::println("{}🤖 LFT - LOW FREQUENCY TRADER{}", colour_cyan, colour_reset);
  std::println("Calibrate → Execute Workflow\n");

  // Same watchlist as live ticker
  auto stocks = std::vector<std::string>{
      "AAPL",  "TSLA", "NVDA", "MSFT", "GOOGL", "AMZN", "META",
      "BRK.B", // Berkshire Hathaway (diversified value)
      "JPM",   // Financials
      "JNJ",   // Healthcare
      "PG",    // Consumer staples
      "XOM",   // Energy
      "ASML",  // EU semiconductors
      "TSM",   // Taiwan Semiconductor
      "NVO",   // Healthcare (Denmark)
      "SAP",   // European software
      "BABA",  // China e-commerce
      "GLD",   // Gold
      "TLT",   // Long-term US bonds
      "IEF",   // Mid-term bonds
      "VNQ"    // Real estate
  };

  auto crypto = std::vector<std::string>{
      "BTC/USD",  "ETH/USD", "SOL/USD", "DOGE/USD",
      "LINK/USD", // Oracles
      "AVAX/USD", // Alternative L1
      "ATOM/USD", // Cosmos ecosystem
  };

  // Phase 1: Calibrate
  auto configs = calibrate_all_strategies(client, stocks, crypto);

  // Check if any strategies are enabled
  auto enabled_count = 0;
  for (const auto &[name, config] : configs) {
    if (config.enabled)
      ++enabled_count;
  }

  if (enabled_count == 0)
    std::println("{}⚠ No profitable strategies - will only manage exits{}\n",
                 colour_yellow, colour_reset);

  // Phase 2: Live trading (runs for 1 hour, then exits)
  run_live_trading(client, stocks, crypto, configs);

  return 0;
}
