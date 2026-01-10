#include "alpaca_client.h"
#include "defs.h"
#include "strategies.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <iomanip>
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
#include "exit_logic_tests.h"

using namespace std::chrono_literals;
using namespace lft; // Import constants from defs.h

namespace {

// ANSI colour codes
constexpr auto colour_reset = "\033[0m";
constexpr auto colour_green = "\033[32m";
constexpr auto colour_red = "\033[31m";
constexpr auto colour_cyan = "\033[36m";
constexpr auto colour_yellow = "\033[33m";

// Dip threshold uses _pc literal from exit_logic_tests.h
// constexpr auto dip_threshold = -2_pc;  // Disabled with dip strategy

// Position tracking for backtest
struct Position {
  std::string symbol;
  std::string strategy;
  double entry_price{};
  double quantity{};
  std::string entry_time;
  std::size_t entry_bar_index{}; // For time-based exit in backtest
  double peak_price{};
  // Exit criteria are now unified (see exit_logic_tests.h)
};

struct BacktestStats {
  std::map<std::string, lft::StrategyStats> strategy_stats;
  double cash{100000.0}; // Starting capital
  std::map<std::string, Position> positions;
  int total_trades{};
  int winning_trades{};
  int losing_trades{};
};

// Parse ISO 8601 timestamp from bar data
std::chrono::system_clock::time_point
parse_bar_timestamp(const std::string &timestamp) {
  // Alpaca bar timestamps are like: "2026-01-08T14:30:00Z"
  std::tm tm = {};
  std::istringstream ss(timestamp);
  ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// Forward declare get_market_status (defined later)
struct MarketStatus {
  bool is_open{};
  std::string message;
};
MarketStatus get_market_status(const std::chrono::system_clock::time_point &);

// Calculate sleep duration to align to 35 seconds past the next minute
// Alpaca recalculates bars at :30 past each minute to include late trades
// Polling at :35 ensures we get the final, recalculated bar data
std::chrono::seconds sleep_until_bar_ready(const std::chrono::system_clock::time_point& now) {
  using namespace std::chrono;

  // Get current time broken down
  auto now_t = system_clock::to_time_t(now);
  auto now_tm = *std::localtime(&now_t);

  // Calculate target: 35 seconds past the NEXT minute
  auto current_second = now_tm.tm_sec;

  // If we're before :35, target is :35 this minute
  // If we're at or after :35, target is :35 next minute
  auto seconds_to_wait = 0;

  if (current_second < 35) {
    // Wait until :35 this minute
    seconds_to_wait = 35 - current_second;
  } else {
    // Wait until :35 next minute (60 - current + 35)
    seconds_to_wait = 60 - current_second + 35;
  }

  return seconds{seconds_to_wait};
}

void process_bar(const std::string &symbol, const lft::Bar &bar,
                 std::size_t bar_index, lft::PriceHistory &history,
                 const std::map<std::string, lft::PriceHistory> &all_histories,
                 BacktestStats &stats,
                 const std::map<std::string, lft::StrategyConfig> &configs) {

  // Filter out extended hours for stocks (keep crypto 24/7)
  auto is_crypto = symbol.find('/') != std::string::npos;
  if (not is_crypto) {
    auto bar_time = parse_bar_timestamp(bar.timestamp);
    auto market_status = get_market_status(bar_time);

    if (not market_status.is_open) {
      return; // Skip this bar - outside regular trading hours (9:30 AM - 4:00
              // PM ET)
    }
  }

  history.add_bar(bar.close, bar.high, bar.low, bar.volume);

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

    // Exit conditions using unified exit criteria
    auto trailing_stop_price = pos.peak_price * (1.0 - trailing_stop_pct);
    auto trailing_stop_triggered = sell_price < trailing_stop_price;

    auto should_exit = pl_pct >= take_profit_pct or pl_pct <= -stop_loss_pct or
                       trailing_stop_triggered;

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
    // Noise regime detection: classify market conditions
    auto recent_noise = history.recent_noise(20);
    auto low_noise_regime =
        recent_noise > 0.0 and recent_noise < 0.005; // <0.5% noise
    auto high_noise_regime = recent_noise > 0.015;   // >1.5% noise

    // Evaluate entry strategies
    auto signals = std::vector<lft::StrategySignal>{
        // lft::Strategies::evaluate_dip(history, dip_threshold),  // Disabled
        lft::Strategies::evaluate_ma_crossover(history),
        lft::Strategies::evaluate_mean_reversion(history),
        lft::Strategies::evaluate_volatility_breakout(history),
        lft::Strategies::evaluate_relative_strength(history, all_histories),
        lft::Strategies::evaluate_volume_surge(history)};

    // Count signals and execute trades ONLY for enabled strategies
    for (auto signal : signals) {
      // Only process signals from enabled strategies (during calibration, only
      // one is enabled)
      if (not configs.contains(signal.strategy_name) or
          not configs.at(signal.strategy_name).enabled)
        continue;

      // Apply low-volume confidence filter
      auto vol_factor = history.volume_factor();
      signal.confidence /=
          vol_factor; // Reduce confidence in low-volume conditions

      // Noise regime filtering: disable momentum strategies in high noise
      if (high_noise_regime and
          (signal.strategy_name == "ma_crossover" or
           signal.strategy_name == "volatility_breakout" or
           signal.strategy_name == "volume_surge")) {
        continue; // Skip momentum strategies in noisy conditions
      }

      // Noise regime filtering: disable mean reversion in low noise
      if (low_noise_regime and signal.strategy_name == "mean_reversion") {
        continue; // Skip mean reversion in trending conditions
      }

      if (signal.should_buy) {
        ++stats.strategy_stats[signal.strategy_name].signals_generated;

        // Filter low-confidence signals (reject if confidence <0.7)
        constexpr auto min_confidence = 0.7;
        if (signal.confidence < min_confidence)
          continue;

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

          // Calculate adaptive TP/SL based on recent noise
          stats.positions[symbol] =
              Position{.symbol = symbol,
                       .strategy = signal.strategy_name,
                       .entry_price = buy_price, // Store actual buy price (ask)
                       .quantity = quantity,
                       .entry_time = bar.timestamp,
                       .entry_bar_index = bar_index,
                       .peak_price = bar.close}; // Peak tracks mid price

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
  // stats.strategy_stats["dip"] = lft::StrategyStats{"dip"};  // Disabled
  stats.strategy_stats["ma_crossover"] = lft::StrategyStats{"ma_crossover"};
  stats.strategy_stats["mean_reversion"] = lft::StrategyStats{"mean_reversion"};
  stats.strategy_stats["volatility_breakout"] =
      lft::StrategyStats{"volatility_breakout"};
  stats.strategy_stats["relative_strength"] =
      lft::StrategyStats{"relative_strength"};
  stats.strategy_stats["volume_surge"] = lft::StrategyStats{"volume_surge"};

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
      lft::StrategyConfig{.name = strategy_name, .enabled = true};

  auto stats = run_backtest_with_data(symbol_bars, configs);
  auto profit = stats.strategy_stats[strategy_name].net_profit();
  auto trades = stats.strategy_stats[strategy_name].trades_closed;
  auto signals = stats.strategy_stats[strategy_name].signals_generated;
  auto win_rate = stats.strategy_stats[strategy_name].win_rate();

  auto config = configs[strategy_name];
  config.trades_closed = trades;
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

  // Fetch historic data ONCE upfront (huge speedup!)
  std::println("\n{}📥 Fetching historic data{}", colour_yellow, colour_reset);
  std::println("{:-<50}", "");
  std::println("{:<10} {:>15} {:>15}", "SYMBOL", "BARS", "TYPE");
  std::println("{:-<50}", "");

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
      auto asset_type = is_crypto ? "Crypto" : "Stock";
      std::println("{:<10} {:>15} {:>15}", symbol, symbol_bars[symbol].size(),
                   asset_type);
    }
  }

  std::println("{:-<50}", "");
  std::println("{}✓ Data fetched - ready for calibration{}\n", colour_green,
               colour_reset);

  auto strategies = std::vector<std::string>{
      // "dip",  // Disabled: loses heavily with tight stops
      "ma_crossover", "mean_reversion", "volatility_breakout",
      "relative_strength", "volume_surge"};

  // Calibrate all strategies in parallel using threads
  auto strategy_configs = std::vector<lft::StrategyConfig>(strategies.size());
  auto threads = std::vector<std::thread>{};

  for (auto i = 0uz; i < strategies.size(); ++i) {
    threads.emplace_back([i, &strategies, &strategy_configs, &symbol_bars]() {
      auto config = calibrate_strategy(strategies[i], symbol_bars);

      // Enable if profitable AND has sufficient trade history
      config.enabled = config.net_profit > 0.0 and
                       config.trades_closed >= min_trades_to_enable;

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
  auto is_crypto = symbol.find('/') != std::string::npos;
  auto status = std::string{};
  auto colour = colour_reset;

  // Use timestamp-aware method to avoid adding stale trades
  history.add_price_with_timestamp(snap.latest_trade_price,
                                   snap.latest_trade_timestamp);

  if (history.has_history) {
    if (history.change_percent > 0.0)
      colour = colour_green;
    else if (history.change_percent < 0.0)
      colour = colour_red;

    // Check for outliers first (extreme moves), then regular alerts
    if (is_outlier(history.change_percent))
      status = "⚠️ OUTLIER";
    else if (is_alert(history.change_percent, is_crypto))
      status = "🚨 ALERT";
  }

  std::println("{}{:<10} {:>12.2f} {:>12.2f} {:>12.2f} {:>9.2f}% {}{}", colour,
               symbol, snap.latest_trade_price, snap.latest_quote_bid,
               snap.latest_quote_ask, history.change_percent, status,
               colour_reset);
}

// Check if US stock market is open and time until open/close
MarketStatus
get_market_status(const std::chrono::system_clock::time_point &now) {
  using namespace std::chrono;

  // Convert to time_t to get weekday
  auto now_t = system_clock::to_time_t(now);
  auto utc_time = std::gmtime(&now_t);
  auto weekday = utc_time->tm_wday;

  // Market closed on weekends
  if (weekday == 0 or weekday == 6)
    return {false, "Market CLOSED (weekend)"};

  // US Eastern Time offset (simplified DST: EST=UTC-5, EDT=UTC-4)
  auto month = utc_time->tm_mon;
  auto is_dst = (month >= 2 and month <= 9); // Mar-Oct = EDT
  auto et_offset = hours{is_dst ? -4 : -5};

  // Get current time in ET
  auto et_now = now + et_offset;
  auto et_time_t = system_clock::to_time_t(et_now);
  auto et_tm = std::gmtime(&et_time_t);

  auto current_time = hours{et_tm->tm_hour} + minutes{et_tm->tm_min};
  constexpr auto market_open = 9h + 30min; // 9:30 AM ET
  constexpr auto market_close = 16h;       // 4:00 PM ET

  if (current_time >= market_open and current_time < market_close) {
    auto time_until_close = market_close - current_time;
    auto h = duration_cast<hours>(time_until_close).count();
    auto m = duration_cast<minutes>(time_until_close % 1h).count();
    return {true, std::format("Market OPEN - {}h {}m until close", h, m)};
  } else if (current_time < market_open) {
    auto time_until_open = market_open - current_time;
    auto h = duration_cast<hours>(time_until_open).count();
    auto m = duration_cast<minutes>(time_until_open % 1h).count();
    return {false, std::format("Market CLOSED - {}h {}m until open", h, m)};
  } else {
    // After close - time until tomorrow's open
    auto time_until_tomorrow = (24h - current_time) + market_open;
    auto h = duration_cast<hours>(time_until_tomorrow).count();
    auto m = duration_cast<minutes>(time_until_tomorrow % 1h).count();
    return {false, std::format("Market CLOSED - {}h {}m until open", h, m)};
  }
}

void print_account_stats(lft::AlpacaClient &client,
                         const std::chrono::system_clock::time_point &now) {
  auto account_result = client.get_account();
  if (not account_result)
    return;

  auto account_json =
      nlohmann::json::parse(account_result.value(), nullptr, false);
  if (account_json.is_discarded())
    return;

  auto cash = std::stod(account_json["cash"].get<std::string>());
  auto buying_power =
      std::stod(account_json["buying_power"].get<std::string>());
  auto equity = std::stod(account_json["equity"].get<std::string>());
  auto daytrading_buying_power =
      std::stod(account_json["daytrading_buying_power"].get<std::string>());
  auto long_market_value =
      std::stod(account_json["long_market_value"].get<std::string>());
  auto is_pdt = account_json["pattern_day_trader"].get<bool>();
  auto daytrade_count = account_json["daytrade_count"].get<int>();
  auto trading_blocked = account_json["trading_blocked"].get<bool>();

  std::println("\n💰 ACCOUNT STATUS");
  std::println("{:-<70}", "");
  std::println("Cash:              ${:>12.2f}", cash);
  std::println("Equity:            ${:>12.2f}", equity);
  std::println("Long Positions:    ${:>12.2f}", long_market_value);
  std::println("Buying Power:      ${:>12.2f}", buying_power);

  // Day trading buying power with warning
  if (daytrading_buying_power <= 0.0) {
    std::println("{}Day Trade BP:      ${:>12.2f}  ⚠️  EXHAUSTED{}",
                 colour_red, daytrading_buying_power, colour_reset);
  } else if (daytrading_buying_power < notional_amount) {
    std::println("{}Day Trade BP:      ${:>12.2f}  ⚠️  LOW{}",
                 colour_yellow, daytrading_buying_power, colour_reset);
  } else {
    std::println("Day Trade BP:      ${:>12.2f}", daytrading_buying_power);
  }

  // Pattern day trader status with day trade count
  auto pdt_colour = is_pdt ? colour_yellow : colour_reset;
  std::println("{}Pattern Day Trader: {} ({}/3 day trades used){}",
               pdt_colour, is_pdt ? "YES" : "NO", daytrade_count,
               colour_reset);

  // Trading status
  auto status_text = trading_blocked ? "BLOCKED" : "ACTIVE";
  auto status_icon = trading_blocked ? "❌" : "✅";
  auto trading_colour = trading_blocked ? colour_red : colour_green;
  std::println("{}Trading Status:     {} {}{}",
               trading_colour, status_text, status_icon, colour_reset);

  // Market status
  auto market_status = get_market_status(now);
  auto market_colour = market_status.is_open ? colour_green : colour_red;
  std::println("{}{}{}", market_colour, market_status.message, colour_reset);

  // Warnings
  if (trading_blocked) {
    std::println("{}❌ CRITICAL: Trading blocked - cannot place orders{}",
                 colour_red, colour_reset);
  }
  if (daytrading_buying_power <= 0.0) {
    std::println("{}⚠  WARNING: No day trading buying power - new positions may be rejected{}",
                 colour_yellow, colour_reset);
  }
  if (cash < notional_amount) {
    std::println("{}⚠  WARNING: Cash (${:.2f}) below trade size (${:.2f}){}",
                 colour_yellow, cash, notional_amount, colour_reset);
  }
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

void log_order_entry(std::string_view symbol, std::string_view strategy,
                     std::string_view order_id, double entry_price,
                     double quantity, double notional,
                     const std::chrono::system_clock::time_point &entry_time,
                     double account_balance) {

  auto filename = std::string{"lft_orders.csv"};

  // Check if file exists to determine if we need to write header
  auto file_exists = std::ifstream{filename}.good();

  auto file = std::ofstream{filename, std::ios::app};
  if (not file.is_open()) {
    std::println("{}⚠  Failed to write order log: {}{}", colour_yellow, filename,
                 colour_reset);
    return;
  }

  // Write header if new file
  if (not file_exists)
    file << "timestamp,symbol,strategy,order_id,entry_price,quantity,notional,"
            "account_balance\n";

  // Write order entry data
  file << std::format("{:%Y-%m-%d %H:%M:%S},{},{},{},{:.4f},{:.6f},{:.2f},"
                      "{:.2f}\n",
                      entry_time, symbol, strategy, order_id, entry_price,
                      quantity, notional, account_balance);

  file.close();

  std::println("{}📝 Order logged to: {}{}", colour_green, filename,
               colour_reset);
}

void log_exit(std::string_view symbol, std::string_view order_id,
               std::string_view exit_reason, double exit_price,
               const std::chrono::system_clock::time_point &exit_time,
               double peak_price, double account_balance) {

  auto filename = std::string{"lft_exits.csv"};

  // Check if file exists to determine if we need to write header
  auto file_exists = std::ifstream{filename}.good();

  auto file = std::ofstream{filename, std::ios::app};
  if (not file.is_open()) {
    std::println("{}⚠  Failed to write exit log: {}{}", colour_yellow, filename,
                 colour_reset);
    return;
  }

  // Write header if new file
  if (not file_exists)
    file << "timestamp,symbol,order_id,exit_price,exit_reason,peak_price,"
            "account_balance\n";

  // Write exit data
  file << std::format("{:%Y-%m-%d %H:%M:%S},{},{},{:.4f},{},{:.4f},{:.2f}\n",
                      exit_time, symbol, order_id, exit_price, exit_reason,
                      peak_price, account_balance);

  file.close();

  std::println("{}📝 Exit logged to: {}{}", colour_green, filename,
               colour_reset);
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
  auto position_peaks = std::map<std::string, double>{};
  auto position_entry_times =
      std::map<std::string, std::chrono::system_clock::time_point>{};
  auto position_order_ids = std::map<std::string, std::string>{};
  auto strategy_stats = std::map<std::string, lft::StrategyStats>{};
  auto api_positions = std::set<std::string>{}; // API is source of truth

  // Initialise stats for ALL strategies (enabled and disabled)
  for (const auto &[name, config] : configs)
    strategy_stats[name] = lft::StrategyStats{name};

  auto cycle = 0;

  while (cycle < max_cycles) {
    ++cycle;
    auto now = std::chrono::system_clock::now();
    std::println("\n{:-<70}", "");
    std::println("Tick at {:%Y-%m-%d %H:%M:%S}", now);
    std::println("{:-<70}", "");

    // Display account status
    print_account_stats(client, now);

    // Fetch and process positions (API is source of truth)
    auto positions_result = client.get_positions();

    if (positions_result) {
      auto positions_json =
          nlohmann::json::parse(positions_result.value(), nullptr, false);

      if (not positions_json.is_discarded()) {
        // Extract symbol names from API positions (API is source of truth)
        api_positions.clear();
        for (const auto &pos : positions_json)
          api_positions.insert(pos["symbol"].get<std::string>());

        if (not positions_json.empty()) {
          std::println("\n📊 OPEN POSITIONS");
          std::println("{:-<82}", "");
          std::println("{:<10} {:>10} {:>15} {:>15} {:>15} {:>10}", "SYMBOL",
                       "QTY", "ENTRY PRICE", "CURRENT PRICE", "MARKET VALUE",
                       "P&L %");
          std::println("{:-<82}", "");

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

            std::println(
                "{}{:<10} {:>10} {:>15.2f} {:>15.2f} {:>15.2f} {:>9.2f}%{}",
                colour, symbol, qty, avg_entry, current, market_value,
                unrealized_plpc, colour_reset);
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

            // Get strategy config - use default for orphaned positions
            // Determine strategy name (if known from this session)
            auto strategy = position_strategies.contains(symbol)
                                ? position_strategies[symbol]
                                : std::string{"unknown"};

            // Update peak price
            if (not position_peaks.contains(symbol))
              position_peaks[symbol] = current_price;
            else if (current_price > position_peaks[symbol])
              position_peaks[symbol] = current_price;

            // Exit using unified exit criteria
            auto peak = position_peaks[symbol];
            auto trailing_stop_price = peak * (1.0 - trailing_stop_pct);
            auto trailing_stop_triggered = current_price < trailing_stop_price;

            auto should_exit = pl_pct >= take_profit_pct or
                               pl_pct <= -stop_loss_pct or
                               trailing_stop_triggered;

            if (should_exit) {
              auto profit_percent = (unrealized_pl / cost_basis) * 100.0;

              auto exit_reason = std::string{};
              if (trailing_stop_triggered)
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

                // Log the exit with full details
                if (position_entry_times.contains(symbol)) {
                  // Get current account balance
                  auto account_balance = 0.0;
                  auto account_result = client.get_account();
                  if (account_result) {
                    auto account_json = nlohmann::json::parse(
                        account_result.value(), nullptr, false);
                    if (not account_json.is_discarded())
                      account_balance = std::stod(
                          account_json["portfolio_value"].get<std::string>());
                  }

                  // Log exit with order ID if we have it
                  auto order_id = position_order_ids.contains(symbol)
                                      ? position_order_ids[symbol]
                                      : "unknown";
                  log_exit(symbol, order_id, exit_reason, current_price, now,
                           peak, account_balance);
                }

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
              // lft::Strategies::evaluate_dip(history, dip_threshold),  //
              // Disabled
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

          if (not api_positions.contains(symbol)) {
            // Execute first enabled signal
            for (const auto &signal : signals) {
              if (signal.should_buy and
                  configs.contains(signal.strategy_name) and
                  configs.at(signal.strategy_name).enabled) {

                // Check trade eligibility (spread and volume filters)
                auto is_crypto = symbol.find('/') != std::string::npos;
                auto max_spread = is_crypto ? max_spread_bps_crypto : max_spread_bps_stocks;

                if (not lft::Strategies::is_tradeable(snap, history, max_spread, min_volume_ratio)) {
                  auto spread_bps = lft::Strategies::calculate_spread_bps(snap);
                  auto vol_ratio = lft::Strategies::calculate_volume_ratio(history);

                  std::println("{}⛔ TRADE BLOCKED: {}{}", colour_yellow, symbol, colour_reset);
                  std::println("   Spread: {:.1f} bps (max {:.1f}), Volume: {:.1f}x avg (min {:.1f}x)",
                               spread_bps, max_spread, vol_ratio, min_volume_ratio);
                  std::println("   Signal: {} - {}", signal.strategy_name, signal.reason);
                  break;  // Skip this trade and move to next symbol
                }

                std::println("{}🚨 SIGNAL: {} - {}{}", colour_cyan,
                             signal.strategy_name, signal.reason, colour_reset);
                std::println("   Buying ${:.0f} of {}...", notional_amount,
                             symbol);

                auto order = client.place_order(symbol, "buy", notional_amount);
                if (order) {
                  // Parse order response to verify status
                  auto order_json =
                      nlohmann::json::parse(order.value(), nullptr, false);
                  if (not order_json.is_discarded()) {
                    auto order_id = order_json.value("id", "unknown");
                    auto status = order_json.value("status", "unknown");
                    auto side = order_json.value("side", "unknown");
                    auto notional_str = order_json.value("notional", "0");

                    std::println(
                        "✅ Order placed: ID={} status={} side={} notional=${}",
                        order_id, status, side, notional_str);

                    // Only count as executed if order is accepted
                    if (status == "accepted" or status == "pending_new" or
                        status == "filled") {
                      position_strategies[symbol] = signal.strategy_name;
                      position_entry_times[symbol] = now;
                      position_order_ids[symbol] = order_id;
                      ++strategy_stats[signal.strategy_name].trades_executed;

                      // Log order entry with account balance
                      auto account_result = client.get_account();
                      auto balance = 0.0;
                      if (account_result) {
                        auto account_json = nlohmann::json::parse(
                            account_result.value(), nullptr, false);
                        if (not account_json.is_discarded())
                          balance = std::stod(
                              account_json["equity"].get<std::string>());
                      }

                      // Estimate filled price and quantity from order response
                      auto filled_price =
                          order_json.contains("filled_avg_price")
                              ? std::stod(
                                    order_json["filled_avg_price"].get<std::string>())
                              : 0.0;
                      auto quantity_filled =
                          order_json.contains("filled_qty")
                              ? std::stod(order_json["filled_qty"].get<std::string>())
                              : notional_amount / filled_price;

                      log_order_entry(symbol, signal.strategy_name, order_id,
                                      filled_price, quantity_filled, notional_amount,
                                      now, balance);
                    } else {
                      std::println("{}⚠  Order status '{}' - may not execute{}",
                                   colour_yellow, status, colour_reset);
                    }
                  } else {
                    std::println("✅ Order placed (could not parse response)");
                    position_strategies[symbol] = signal.strategy_name;
                    position_entry_times[symbol] = now;
                    ++strategy_stats[signal.strategy_name].trades_executed;
                  }
                } else {
                  // Order failed - explain why and continue
                  std::println("{}❌ Order failed for {}: {}{}",
                               colour_red, symbol,
                               "likely insufficient buying power or non-marginable security",
                               colour_reset);
                  std::println("{}   Continuing with other symbols...{}",
                               colour_yellow, colour_reset);
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
              // lft::Strategies::evaluate_dip(history, dip_threshold),  //
              // Disabled
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

          if (not api_positions.contains(symbol)) {
            // Execute first enabled signal
            for (const auto &signal : signals) {
              if (signal.should_buy and
                  configs.contains(signal.strategy_name) and
                  configs.at(signal.strategy_name).enabled) {

                // Check trade eligibility (spread and volume filters)
                auto is_crypto = symbol.find('/') != std::string::npos;
                auto max_spread = is_crypto ? max_spread_bps_crypto : max_spread_bps_stocks;

                if (not lft::Strategies::is_tradeable(snap, history, max_spread, min_volume_ratio)) {
                  auto spread_bps = lft::Strategies::calculate_spread_bps(snap);
                  auto vol_ratio = lft::Strategies::calculate_volume_ratio(history);

                  std::println("{}⛔ TRADE BLOCKED: {}{}", colour_yellow, symbol, colour_reset);
                  std::println("   Spread: {:.1f} bps (max {:.1f}), Volume: {:.1f}x avg (min {:.1f}x)",
                               spread_bps, max_spread, vol_ratio, min_volume_ratio);
                  std::println("   Signal: {} - {}", signal.strategy_name, signal.reason);
                  break;  // Skip this trade and move to next symbol
                }

                std::println("{}🚨 SIGNAL: {} - {}{}", colour_cyan,
                             signal.strategy_name, signal.reason, colour_reset);
                std::println("   Buying ${:.0f} of {}...", notional_amount,
                             symbol);

                auto order = client.place_order(symbol, "buy", notional_amount);
                if (order) {
                  // Parse order response to verify status
                  auto order_json =
                      nlohmann::json::parse(order.value(), nullptr, false);
                  if (not order_json.is_discarded()) {
                    auto order_id = order_json.value("id", "unknown");
                    auto status = order_json.value("status", "unknown");
                    auto side = order_json.value("side", "unknown");
                    auto notional_str = order_json.value("notional", "0");

                    std::println(
                        "✅ Order placed: ID={} status={} side={} notional=${}",
                        order_id, status, side, notional_str);

                    // Only count as executed if order is accepted
                    if (status == "accepted" or status == "pending_new" or
                        status == "filled") {
                      position_strategies[symbol] = signal.strategy_name;
                      position_entry_times[symbol] = now;
                      position_order_ids[symbol] = order_id;
                      ++strategy_stats[signal.strategy_name].trades_executed;

                      // Log order entry with account balance
                      auto account_result = client.get_account();
                      auto balance = 0.0;
                      if (account_result) {
                        auto account_json = nlohmann::json::parse(
                            account_result.value(), nullptr, false);
                        if (not account_json.is_discarded())
                          balance = std::stod(
                              account_json["equity"].get<std::string>());
                      }

                      // Estimate filled price and quantity from order response
                      auto filled_price =
                          order_json.contains("filled_avg_price")
                              ? std::stod(
                                    order_json["filled_avg_price"].get<std::string>())
                              : 0.0;
                      auto quantity_filled =
                          order_json.contains("filled_qty")
                              ? std::stod(order_json["filled_qty"].get<std::string>())
                              : notional_amount / filled_price;

                      log_order_entry(symbol, signal.strategy_name, order_id,
                                      filled_price, quantity_filled, notional_amount,
                                      now, balance);
                    } else {
                      std::println("{}⚠  Order status '{}' - may not execute{}",
                                   colour_yellow, status, colour_reset);
                    }
                  } else {
                    std::println("✅ Order placed (could not parse response)");
                    position_strategies[symbol] = signal.strategy_name;
                    position_entry_times[symbol] = now;
                    ++strategy_stats[signal.strategy_name].trades_executed;
                  }
                } else {
                  // Order failed - explain why and continue
                  std::println("{}❌ Order failed for {}: {}{}",
                               colour_red, symbol,
                               "likely insufficient buying power or non-marginable security",
                               colour_reset);
                  std::println("{}   Continuing with other symbols...{}",
                               colour_yellow, colour_reset);
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

    // Calculate sleep duration to align to :35 past next minute
    auto sleep_duration = sleep_until_bar_ready(now);
    auto next_update = std::chrono::floor<std::chrono::seconds>(now + sleep_duration);
    auto cycles_remaining = max_cycles - cycle;

    std::println(
        "\n⏳ Next update at {:%H:%M:%S} - {} seconds | {} cycles until re-calibration\n",
        next_update, sleep_duration.count(), cycles_remaining);
    std::this_thread::sleep_for(sleep_duration);
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

  // 10-second countdown before live trading starts
  std::println(
      "\n{}⚠ STARTING LIVE TRADING IN 10 SECONDS - Press Ctrl+C to cancel{}",
      colour_yellow, colour_reset);
  for (auto i = 10; i > 0; --i) {
    std::println("{}...", i);
    std::this_thread::sleep_for(1s);
  }
  std::println("{}🚀 GO!{}\n", colour_green, colour_reset);

  // Phase 2: Live trading (runs for 1 hour, then exits)
  run_live_trading(client, stocks, crypto, configs);

  return 0;
}
