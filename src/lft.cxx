//
// CRITICAL BUG FIXES (2026-01-13):
//
// Three related bugs fixed to prevent duplicate orders:
//
// 1. NON-UNIQUE client_order_id (Lines 77-93):
//    - Bug: All orders for same strategy/params had identical client_order_id
//    - Symptom: Alpaca rejected with "client_order_id must be unique" (error 40010001)
//    - Fix: Include symbol + millisecond timestamp in client_order_id
//    - Format: "TLT_momentum_1768317100944|tp:2.0|sl:-5.0|ts:30.0"
//
// 2. AGGRESSIVE CLEANUP LOOP (Lines 1280-1310):
//    - Bug: Cleanup erased symbols from position_strategies if not in API response
//    - Root cause: Unknown - API lag, race condition, or timing issue (under investigation)
//    - Symptom: 3 simultaneous ETH/USD positions at 15:11:41, 15:12:39, 15:13:38
//    - Fix: 5-minute grace period - don't erase recently entered symbols
//
// 3. MISSING DUPLICATE GUARDS (Lines 1331-1343, 1457, 1468):
//    - Bug: Only checked symbols_in_use (from API), not persistent position_strategies
//    - Fix: Two-layer check: symbols_in_use AND position_strategies
//    - Also: Immediately add symbol to symbols_in_use after order placement
//
// See inline comments at each location for detailed explanations.
//

#include "alpaca_client.h"
#include "defs.h"
#include "strategies.h"
#include <algorithm>
#include <cassert>
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

#ifdef HAVE_FTXUI
#include "tui.hxx"
#endif

// Include compile-time exit logic tests
// This header defines: operator""_pc, stock_spread, crypto_spread,
// take_profit_pct, stop_loss_pct, trailing_stop_pct
#include "exit_logic_tests.h"

using namespace std::chrono_literals;
using namespace std::string_view_literals;
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

// Trading constants
constexpr auto starting_capital = 100000.0;
constexpr auto low_noise_threshold = 0.005;  // 0.5% noise
constexpr auto high_noise_threshold = 0.015; // 1.5% noise
constexpr auto dst_start_month = 2;          // March (0-indexed)
constexpr auto dst_end_month = 9;            // October (0-indexed)
constexpr auto et_offset_dst = -4h;          // EDT (daylight saving)
constexpr auto et_offset_std = -5h;          // EST (standard time)

// CSV file constants
constexpr auto blocked_csv_filename = "lft_blocked_trades.csv"sv;
constexpr auto blocked_csv_header =
    "timestamp,symbol,strategy,signal_reason,spread_bps,max_spread_bps,volume_ratio,min_volume_ratio,block_reason\n"sv;

// Compile-time validation of trading constants
static_assert(starting_capital > 0.0, "Starting capital must be positive");
static_assert(starting_capital >= 1000.0,
              "Starting capital too low - min $1000");
static_assert(low_noise_threshold > 0.0,
              "Low noise threshold must be positive");
static_assert(high_noise_threshold > low_noise_threshold,
              "High noise threshold must be higher than low noise threshold");
static_assert(high_noise_threshold < 1.0,
              "High noise threshold too high - would never trigger");
static_assert(dst_start_month >= 0 and dst_start_month <= 11,
              "DST start month must be 0-11 (Jan-Dec)");
static_assert(dst_end_month >= 0 and dst_end_month <= 11,
              "DST end month must be 0-11 (Jan-Dec)");
static_assert(dst_end_month > dst_start_month,
              "DST end month must be after start month");

// Market status information
struct MarketStatus {
  bool is_open{};
  std::string message;
};

// Encode trading parameters into client_order_id (max 48 chars)
// Includes symbol and timestamp to ensure uniqueness across orders
std::string encode_client_order_id(std::string_view strategy,
                                    std::string_view symbol,
                                    double take_profit_pct, double stop_loss_pct,
                                    double trailing_stop_pct) {
  // Get timestamp in milliseconds since epoch
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()).count();

  // Format: "SYM_strategy_ms" (symbol ensures uniqueness per ticker)
  // Keep it under 48 chars: symbol(5) + strategy(10) + timestamp(13) + separators = ~30 chars
  // Convert fractions to percentages for display (0.02 -> 2.0)
  return std::format("{}_{}_{}|tp:{:.1f}|sl:{:.1f}|ts:{:.1f}",
                     symbol, strategy, ms,
                     take_profit_pct * 100.0, stop_loss_pct * 100.0, trailing_stop_pct * 100.0);
}

// Decode client_order_id back to strategy name
// Format: "SYM_strategy_timestamp|tp:2.0|sl:-5.0|ts:0.5"
std::string decode_strategy(std::string_view client_order_id) {
  // Find the pipe separator between ID and params
  auto pipe_pos = client_order_id.find('|');
  auto id_part = pipe_pos != std::string_view::npos
                     ? client_order_id.substr(0, pipe_pos)
                     : client_order_id;

  // Extract strategy name from "SYM_strategy_timestamp"
  // Find second underscore to skip "SYM_"
  auto first_under = id_part.find('_');
  if (first_under == std::string_view::npos)
    return std::string{id_part}; // Fallback: return as-is

  auto second_under = id_part.find('_', first_under + 1);
  if (second_under == std::string_view::npos)
    return std::string{id_part.substr(first_under + 1)}; // "strategy_timestamp"

  // Return just the strategy name (between first and second underscore)
  return std::string{id_part.substr(first_under + 1, second_under - first_under - 1)};
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
  auto is_dst = (month >= dst_start_month and month <= dst_end_month);
  auto et_offset = is_dst ? et_offset_dst : et_offset_std;

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

// Position tracking for backtest
struct Position {
  std::string symbol;
  std::string strategy;
  double entry_price{};
  double quantity{};
  std::string entry_time;
  std::size_t entry_bar_index{}; // Bar index when entered
  std::size_t exit_bar_index{};  // Bar index when exited
  double peak_price{};
  // Exit criteria are now unified (see exit_logic_tests.h)
};

struct BacktestStats {
  std::map<std::string, lft::StrategyStats> strategy_stats;
  double cash{starting_capital};
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

// Check if symbol is a crypto pair (contains '/')
constexpr bool is_crypto(std::string_view symbol) {
  return symbol.find('/') != std::string_view::npos;
}

// Get appropriate spread for asset type (from exit_logic_tests.h)
constexpr double get_spread(bool crypto) {
  return crypto ? crypto_spread : stock_spread;
}

// Get appropriate max spread threshold for asset type (from defs.h)
constexpr double get_max_spread_bps(bool crypto) {
  return crypto ? max_spread_bps_crypto : max_spread_bps_stocks;
}

// Check if a file exists
bool file_exists(std::string_view filename) {
  return std::ifstream{filename.data()}.good();
}

// Compile-time tests for helper functions
namespace {
// Test: is_crypto correctly identifies crypto pairs
static_assert(is_crypto("BTC/USD"), "BTC/USD should be identified as crypto");
static_assert(is_crypto("ETH/USD"), "ETH/USD should be identified as crypto");
static_assert(not is_crypto("AAPL"), "AAPL should not be identified as crypto");
static_assert(not is_crypto("TSLA"), "TSLA should not be identified as crypto");

// Test: get_spread returns correct spread for asset type
static_assert(get_spread(true) == crypto_spread,
              "get_spread(true) should return crypto_spread");
static_assert(get_spread(false) == stock_spread,
              "get_spread(false) should return stock_spread");

// Test: get_max_spread_bps returns correct threshold for asset type
static_assert(get_max_spread_bps(true) == max_spread_bps_crypto,
              "get_max_spread_bps(true) should return crypto threshold");
static_assert(get_max_spread_bps(false) == max_spread_bps_stocks,
              "get_max_spread_bps(false) should return stock threshold");
static_assert(get_max_spread_bps(true) >= get_max_spread_bps(false),
              "Crypto spread threshold should be >= stock threshold");
} // namespace

void process_bar(
    const std::string &symbol, const lft::Bar &bar, std::size_t bar_index,
    lft::PriceHistory &history,
    const std::map<std::string, lft::PriceHistory> &all_histories,
    BacktestStats &stats,
    const std::map<std::string, lft::StrategyConfig> &configs,
    const std::map<std::string, std::vector<lft::Bar>> &symbol_bars) {

  // Filter out extended hours for stocks (keep crypto 24/7)
  auto crypto = is_crypto(symbol);
  if (not crypto) {
    auto bar_time = parse_bar_timestamp(bar.timestamp);
    auto market_status = get_market_status(bar_time);

    if (not market_status.is_open)
      return; // Skip this bar - outside regular trading hours (9:30 AM - 4:00
              // PM ET)
  }

  history.add_bar(bar.close, bar.high, bar.low, bar.volume);

  auto has_position = stats.positions.contains(symbol);

  if (has_position) {
    auto &pos = stats.positions[symbol];

    // Apply spread: sell at bid (mid - half spread)
    auto crypto = is_crypto(symbol);
    auto spread = get_spread(crypto);
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

    auto should_exit = pl_pct >= take_profit_pct || pl_pct <= -stop_loss_pct ||
                       trailing_stop_triggered;

    if (should_exit) {
      stats.cash += current_value;

      // Calculate trade duration
      auto duration_bars = bar_index - pos.entry_bar_index;

      auto &strategy_stat = stats.strategy_stats[pos.strategy];
      ++strategy_stat.trades_closed;

      // Track duration statistics
      strategy_stat.total_duration_bars += duration_bars;
      if (duration_bars > strategy_stat.max_duration_bars)
        strategy_stat.max_duration_bars = duration_bars;
      if (duration_bars < strategy_stat.min_duration_bars)
        strategy_stat.min_duration_bars = duration_bars;

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
        recent_noise > 0.0 and recent_noise < low_noise_threshold;
    auto high_noise_regime = recent_noise > high_noise_threshold;

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

      // Apply low-volume confidence filter (reduce confidence in low-volume
      // conditions)
      signal.confidence /= history.volume_factor();

      // Noise regime filtering: disable momentum strategies in high noise
      if (high_noise_regime and
          (signal.strategy_name == "ma_crossover" or
           signal.strategy_name == "volatility_breakout" or
           signal.strategy_name == "volume_surge")) {
        continue; // Skip momentum strategies in noisy conditions
      }

      // Noise regime filtering: disable mean reversion in low noise
      if (low_noise_regime and signal.strategy_name == "mean_reversion")
        continue; // Skip mean reversion in trending conditions

      if (signal.should_buy) {
        ++stats.strategy_stats[signal.strategy_name].signals_generated;

        // Measure forward return for this signal (look 10 bars ahead)
        if (symbol_bars.contains(symbol)) {
          const auto &bars = symbol_bars.at(symbol);
          constexpr auto forward_horizon = 10uz; // 10-bar forward return
          auto future_index = bar_index + forward_horizon;

          if (future_index < bars.size()) {
            auto current_price = bar.close;
            auto future_price = bars[future_index].close;

            assert(current_price > 0.0 && "Current price must be positive");
            assert(future_price > 0.0 && "Future price must be positive");
            assert(std::isfinite(current_price) &&
                   "Current price must be finite");
            assert(std::isfinite(future_price) &&
                   "Future price must be finite");

            auto forward_return_pct =
                (future_price - current_price) / current_price;
            auto forward_return_bps =
                forward_return_pct * 10000.0; // Convert to bps

            assert(std::isfinite(forward_return_bps) &&
                   "Forward return must be finite");
            assert(std::abs(forward_return_bps) < 10000.0 &&
                   "Forward return unreasonably large (>10000 bps = 100%)");

            // Track forward returns
            auto &strat_stats = stats.strategy_stats[signal.strategy_name];
            strat_stats.total_forward_returns_bps += forward_return_bps;
            ++strat_stats.forward_return_samples;
          }
        }

        // Filter low-confidence signals (reject if confidence <0.7)
        constexpr auto min_confidence = 0.7;
        if (signal.confidence < min_confidence)
          continue;

        // Execute if we have cash
        if (stats.cash >= notional_amount) {

          // Apply spread: buy at ask (mid + half spread)
          auto crypto = is_crypto(symbol);
          auto spread = get_spread(crypto);
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

      process_bar(symbol, bar, i, history, price_histories, stats, configs,
                  symbol_bars);
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
    auto crypto = is_crypto(symbol);
    auto spread = get_spread(crypto);
    auto final_mid = bars.back().close;
    auto half_spread = final_mid * (spread / 2.0);
    auto final_sell_price = final_mid - half_spread;

    auto current_value = pos.quantity * final_sell_price;
    auto cost_basis = pos.quantity * pos.entry_price;
    auto unrealized_pl = current_value - cost_basis;

    stats.cash += current_value;

    // Calculate trade duration (EOD exit - last bar)
    auto duration_bars = (bars.size() - 1) - pos.entry_bar_index;

    auto &strategy_stat = stats.strategy_stats[pos.strategy];
    ++strategy_stat.trades_closed;

    // Track duration statistics
    strategy_stat.total_duration_bars += duration_bars;
    if (duration_bars > strategy_stat.max_duration_bars)
      strategy_stat.max_duration_bars = duration_bars;
    if (duration_bars < strategy_stat.min_duration_bars)
      strategy_stat.min_duration_bars = duration_bars;

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
  auto expected_move =
      stats.strategy_stats[strategy_name].avg_forward_return_bps();
  auto avg_duration = stats.strategy_stats[strategy_name].avg_duration_bars();

  auto config = configs[strategy_name];
  config.trades_closed = trades;
  config.net_profit = profit;
  config.win_rate = win_rate;
  config.expected_move_bps = expected_move;

  {
    auto lock = std::scoped_lock{calibration_print_mutex};
    auto colour = profit > 0.0 ? colour_green : colour_red;
    std::println(
        "{}✓ {} Complete: {} signals, {} trades, ${:.2f} P&L, {:.1f}% WR, "
        "{:.1f} bps avg move, {:.1f} bars avg duration{}",
        colour, strategy_name, signals, trades, profit, win_rate, expected_move,
        avg_duration, colour_reset);
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
    auto crypto = is_crypto(symbol);
    auto bars = crypto ? client.get_crypto_bars(symbol, "15Min", start, end)
                       : client.get_bars(symbol, "15Min", start, end);

    if (bars) {
      symbol_bars[symbol] = std::move(*bars);
      auto asset_type = crypto ? "Crypto" : "Stock";
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

// Validate bid/ask quote data
constexpr bool has_valid_quotes(double bid, double ask) {
  return bid > 0.0 and ask > 0.0 and ask >= bid;
}

// Calculate bid-ask spread percentage
constexpr double calculate_spread_pct(double bid, double ask) {
  if (not has_valid_quotes(bid, ask))
    return 0.0;

  auto spread_abs = ask - bid;
  auto mid_price = (bid + ask) / 2.0;
  return (spread_abs / mid_price) * 100.0;
}

void print_header() {
  std::println("\n{:<10} {:>12} {:>12} {:>12} {:>10} {:>8} {}", "SYMBOL",
               "LAST", "BID", "ASK", "CHANGE%", "BID-ASK%", "STATUS");
  std::println("{:-<80}", "");
}

void print_snapshot(const std::string &symbol, const lft::Snapshot &snap,
                    lft::PriceHistory &history) {
  auto crypto = is_crypto(symbol);
  auto status = std::string{};
  auto colour = colour_reset;

  // Use timestamp-aware method to avoid adding stale trades
  history.add_price_with_timestamp(snap.latest_trade_price,
                                   snap.latest_trade_timestamp);

  // Add volume from snapshot if available
  if (snap.minute_bar_volume > 0) {
    history.volumes.push_back(snap.minute_bar_volume);
    // Keep synced with prices (max 100 data points)
    if (history.volumes.size() > 100)
      history.volumes.erase(history.volumes.begin());
  }

  // Calculate bid-ask spread for trading viability assessment
  auto valid_quotes =
      has_valid_quotes(snap.latest_quote_bid, snap.latest_quote_ask);
  auto spread_pct =
      calculate_spread_pct(snap.latest_quote_bid, snap.latest_quote_ask);

  if (history.has_history) {
    if (history.change_percent > 0.0)
      colour = colour_green;
    else if (history.change_percent < 0.0)
      colour = colour_red;

    // Check for outliers first (extreme moves), then regular alerts
    if (is_outlier(history.change_percent))
      status = "⚠️ OUTLIER";
    else if (is_alert(history.change_percent, crypto))
      status = "🚨 ALERT";
  }

  // Flag invalid quote data
  if (not valid_quotes)
    status += status.empty() ? "⚠️ NO QUOTES" : " ⚠️Q";

  // Assess trading viability based on spread
  // Wide spreads indicate poor liquidity and higher transaction costs
  if (valid_quotes and spread_pct > 2.0)
    status += status.empty() ? "💸 WIDE SPREAD" : " 💸";
  else if (valid_quotes and spread_pct > 1.0)
    status += status.empty() ? "⚠️ HIGH SPREAD" : " ⚠️$";

  // Format bid/ask/spread with --- for invalid quotes
  if (valid_quotes)
    std::println(
        "{}{:<10} {:>12.2f} {:>12.2f} {:>12.2f} {:>9.2f}% {:>7.3f}% {}{}", colour,
        symbol, snap.latest_trade_price, snap.latest_quote_bid,
        snap.latest_quote_ask, history.change_percent, spread_pct, status,
        colour_reset);
  else
    std::println(
        "{}{:<10} {:>12.2f} {:>12} {:>12} {:>9.2f}% {:>8} {}{}", colour,
        symbol, snap.latest_trade_price, "---", "---",
        history.change_percent, "---", status, colour_reset);
}

// Calculate total estimated costs for a trade in basis points
constexpr double calculate_total_cost_bps(double spread_bps) {
  assert(spread_bps >= 0.0 && "Spread cannot be negative");
  assert(spread_bps < 1000.0 && "Spread unreasonably high (>1000 bps)");

  // Total cost = spread + slippage buffer + adverse selection
  auto total = spread_bps + slippage_buffer_bps + adverse_selection_bps;

  assert(total > 0.0 && "Total cost must be positive");
  assert(total < 200.0 && "Total cost unreasonably high (>200 bps)");

  return total;
}

// Check if trade has positive edge after costs
constexpr bool has_positive_edge(double expected_move_bps,
                                 double total_cost_bps) {
  assert(expected_move_bps >= 0.0 && "Expected move cannot be negative");
  assert(total_cost_bps > 0.0 && "Total cost must be positive");
  assert(expected_move_bps < 10000.0 &&
         "Expected move unreasonably high (>10000 bps)");

  auto net_edge_bps = expected_move_bps - total_cost_bps;
  return net_edge_bps >= min_edge_bps;
}

// Compile-time tests for cost calculation logic
namespace {
// Test: Total cost includes all components
constexpr auto test_spread = 10.0;
constexpr auto test_total_cost = calculate_total_cost_bps(test_spread);
static_assert(test_total_cost ==
                  test_spread + slippage_buffer_bps + adverse_selection_bps,
              "Total cost should equal spread + slippage + adverse selection");

// Test: Edge calculation with profitable trade
constexpr auto test_expected_move = 50.0; // 50 bps expected
constexpr auto test_costs = 15.0;         // 15 bps costs
static_assert(
    has_positive_edge(test_expected_move, test_costs),
    "50 bps move - 15 bps costs = 35 bps edge, should pass (min 10 bps)");

// Test: Edge calculation with marginal trade
constexpr auto marginal_move = 20.0;  // 20 bps expected
constexpr auto marginal_costs = 15.0; // 15 bps costs
static_assert(
    not has_positive_edge(marginal_move, marginal_costs),
    "20 bps move - 15 bps costs = 5 bps edge, should fail (min 10 bps)");

// Test: Edge calculation with negative edge
static_assert(not has_positive_edge(10.0, 20.0),
              "10 bps move - 20 bps costs = -10 bps edge, should fail");

// Test: Minimum edge boundary
constexpr auto boundary_move = min_edge_bps + 5.0;
constexpr auto boundary_costs = 5.0;
static_assert(has_positive_edge(boundary_move, boundary_costs),
              "Exactly min_edge_bps net should pass");
} // namespace

void print_account_stats(lft::AlpacaClient &client,
                         const std::chrono::system_clock::time_point &now,
                         double &starting_equity,
                         bool &starting_equity_captured
#ifdef HAVE_FTXUI
                         ,
                         std::shared_ptr<tui::trading_monitor> monitor = nullptr,
                         bool tui_active = false
#endif
) {
  auto account_result = client.get_account();
  if (not account_result)
    return;

  auto account_json =
      nlohmann::json::parse(account_result.value(), nullptr, false);
  if (account_json.is_discarded())
    return;

  // Extract account fields with null checks
  auto cash = (account_json.contains("cash") and not account_json["cash"].is_null())
                  ? std::stod(account_json["cash"].get<std::string>())
                  : 0.0;
  auto buying_power =
      (account_json.contains("buying_power") and
       not account_json["buying_power"].is_null())
          ? std::stod(account_json["buying_power"].get<std::string>())
          : 0.0;
  auto equity =
      (account_json.contains("equity") and not account_json["equity"].is_null())
          ? std::stod(account_json["equity"].get<std::string>())
          : 0.0;

  // Capture starting equity on first call
  if (not starting_equity_captured and equity > 0.0) {
    starting_equity = equity;
    starting_equity_captured = true;
#ifdef HAVE_FTXUI
    if (not tui_active) {
#endif
      std::println("\n{}🚀 Session starting equity: ${{:,.2f}}{}", colour_cyan,
                   starting_equity, colour_reset);
#ifdef HAVE_FTXUI
    }
#endif
  }
  auto daytrading_buying_power =
      (account_json.contains("daytrading_buying_power") and
       not account_json["daytrading_buying_power"].is_null())
          ? std::stod(
                account_json["daytrading_buying_power"].get<std::string>())
          : 0.0;
  auto long_market_value =
      (account_json.contains("long_market_value") and
       not account_json["long_market_value"].is_null())
          ? std::stod(account_json["long_market_value"].get<std::string>())
          : 0.0;
  auto trading_blocked =
      (account_json.contains("trading_blocked") and
       not account_json["trading_blocked"].is_null())
          ? account_json["trading_blocked"].get<bool>()
          : false;

#ifdef HAVE_FTXUI
  // Update TUI with account info (estimate position count from long_market_value)
  if (tui_active and monitor) {
    auto position_count =
        long_market_value > 0.0 ? static_cast<size_t>(long_market_value / 1000.0) : 0uz;
    monitor->update_account(equity, buying_power, position_count);
  }

  // Always show console output if TUI is not active
  if (not tui_active) {
#endif
    std::println("\n💰 ACCOUNT STATUS");
    std::println("{:-<70}", "");

    // Session P&L (if captured)
    if (starting_equity_captured) {
      auto session_pnl = equity - starting_equity;
      auto session_pnl_pct = (session_pnl / starting_equity) * 100.0;
      auto pnl_colour = session_pnl > 0.0 ? colour_green
                      : session_pnl < 0.0 ? colour_red
                      : colour_reset;

      std::println("Starting Equity:   ${:>12.2f}", starting_equity);
      std::println("Current Equity:    ${:>12.2f}", equity);
      std::println("{}Session P&L:       {:+>12.2f} ({:+.2f}%){}", pnl_colour,
                   session_pnl, session_pnl_pct, colour_reset);
      std::println("{:-<70}", "");
    }

    std::println("Cash:              ${:>12.2f}", cash);
    std::println("Long Positions:    ${:>12.2f}", long_market_value);
    std::println("Buying Power:      ${:>12.2f}", buying_power);

    // Day trading buying power with warning
    if (daytrading_buying_power <= 0.0) {
      std::println("{}Day Trade BP:      ${:>12.2f}  ⚠️  EXHAUSTED{}", colour_red,
                   daytrading_buying_power, colour_reset);
    } else if (daytrading_buying_power < notional_amount) {
      std::println("{}Day Trade BP:      ${:>12.2f}  ⚠️  LOW{}", colour_yellow,
                   daytrading_buying_power, colour_reset);
    } else {
      std::println("Day Trade BP:      ${:>12.2f}", daytrading_buying_power);
    }

    // Trading status
    auto status_text = trading_blocked ? "BLOCKED" : "ACTIVE";
    auto status_icon = trading_blocked ? "❌" : "✅";
    auto trading_colour = trading_blocked ? colour_red : colour_green;
    std::println("{}Trading Status:     {} {}{}", trading_colour, status_text,
                 status_icon, colour_reset);

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
      std::println("{}⚠  WARNING: No day trading buying power - new positions "
                   "may be rejected{}",
                   colour_yellow, colour_reset);
    }
    if (cash < notional_amount) {
      std::println("{}⚠  WARNING: Cash (${:.2f}) below trade size (${:.2f}){}",
                   colour_yellow, cash, notional_amount, colour_reset);
    }
#ifdef HAVE_FTXUI
  }
#endif
}


void log_blocked_trade(
    std::string_view symbol, std::string_view strategy,
    std::string_view signal_reason, double spread_bps, double max_spread_bps,
    double volume_ratio, double min_volume_ratio,
    const std::chrono::system_clock::time_point &block_time) {

  // Check if file exists to determine if we need to write header
  auto file = std::ofstream{blocked_csv_filename.data(), std::ios::app};
  if (not file.is_open()) {
    std::println("{}⚠  Failed to write blocked trade log: {}{}", colour_yellow,
                 blocked_csv_filename.data(), colour_reset);
    return;
  }

  // Write header if new file
  if (not file_exists(blocked_csv_filename))
    file << blocked_csv_header;

  // Determine block reason
  auto block_reason = std::string{};
  if (spread_bps > max_spread_bps and volume_ratio < min_volume_ratio)
    block_reason = "spread+volume";
  else if (spread_bps > max_spread_bps)
    block_reason = "spread";
  else if (volume_ratio < min_volume_ratio)
    block_reason = "volume";
  else
    block_reason = "unknown";

  // Write blocked trade data
  file << std::format("{:%Y-%m-%d %H:%M:%S},{},{},{},{:.1f},{:.1f},{:.2f},{"
                      ":.2f},{}\n",
                      block_time, symbol, strategy, signal_reason, spread_bps,
                      max_spread_bps, volume_ratio, min_volume_ratio,
                      block_reason);

  file.close();
}

// Live trading loop
void run_live_trading(
    lft::AlpacaClient &client, const std::vector<std::string> &stocks,
    const std::vector<std::string> &crypto,
    const std::map<std::string, lft::StrategyConfig> &configs
#ifdef HAVE_FTXUI
    ,
    std::shared_ptr<tui::trading_monitor> monitor = nullptr,
    bool tui_active = false
#endif
) {

#ifndef HAVE_FTXUI
  std::println("{}🚀 LIVE TRADING MODE{}", colour_green, colour_reset);
  std::println("Using calibrated exit parameters per strategy");
  std::println("Running for 1 hour, then will re-calibrate\n");
#endif

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

  // Capture starting equity for session P&L tracking
  auto starting_equity = 0.0;
  auto starting_equity_captured = false;

#ifdef HAVE_FTXUI
  auto order_number = 0uz;
  auto cycle_count = 0uz;
#endif

  // Startup: recover open positions from order history
#ifndef HAVE_FTXUI
  std::println("\n{}🔄 Recovering positions from API...{}", colour_cyan, colour_reset);
#endif
  auto all_orders_result = client.get_all_orders();
  if (all_orders_result) {
    auto orders_json =
        nlohmann::json::parse(all_orders_result.value(), nullptr, false);

    if (not orders_json.is_discarded() and orders_json.is_array()) {
      // Recover open positions from order history
      auto positions_result = client.get_positions();
      if (positions_result) {
        auto positions_json =
            nlohmann::json::parse(positions_result.value(), nullptr, false);

        if (not positions_json.is_discarded() and
            positions_json.is_array() and not positions_json.empty()) {

          std::println("\n   Recovering {} open positions...",
                       positions_json.size());

          // For each open position, find the order that created it
          for (const auto &pos : positions_json) {
            auto symbol = pos["symbol"].get<std::string>();

            // Find most recent filled buy order for this symbol
            for (const auto &order : orders_json) {
              if (order["symbol"].get<std::string>() == symbol and
                  order["status"].get<std::string>() == "filled" and
                  order["side"].get<std::string>() == "buy") {

                auto order_id = order["id"].get<std::string>();
                auto client_order_id = order.value("client_order_id", "");

                // Extract strategy from client_order_id (format:
                // "strategy|tp:2.0|sl:-5.0|ts:30.0")
                if (not client_order_id.empty()) {
                  auto strategy = decode_strategy(client_order_id);
                  position_strategies[symbol] = strategy;
                  position_order_ids[symbol] = order_id;
                  std::println("   ✓ {} ({})", symbol, client_order_id);
                } else {
                  position_strategies[symbol] = "unknown";
                  position_order_ids[symbol] = order_id;
                  std::println("   ⚠ {} (strategy unknown)", symbol);
                }
                break; // Found the order for this position
              }
            }
          }
        }
      }
    }
  }
  std::println("");

  for (auto cycle : std::views::iota(0, max_cycles)) {
    auto now = std::chrono::system_clock::now();

#ifdef HAVE_FTXUI
    if (monitor)
      monitor->increment_cycle_count();
#else
    std::println("\n{:-<70}", "");
    std::println("Tick at {:%Y-%m-%d %H:%M:%S}", now);
    std::println("{:-<70}", "");
#endif

    // Calculate current ET time for market hours checks
    auto now_t = std::chrono::system_clock::to_time_t(now);
    auto utc_time = std::gmtime(&now_t);
    auto month = utc_time->tm_mon;
    auto is_dst = (month >= dst_start_month and month <= dst_end_month);
    auto et_offset = is_dst ? et_offset_dst : et_offset_std;
    auto et_now = now + et_offset;
    auto et_time_t = std::chrono::system_clock::to_time_t(et_now);
    auto et_tm = std::gmtime(&et_time_t);
    auto current_time = std::chrono::hours{et_tm->tm_hour} + std::chrono::minutes{et_tm->tm_min};

    // Market hours: 9:30 AM - 4:00 PM ET
    constexpr auto market_open = std::chrono::hours{9} + std::chrono::minutes{30};
    constexpr auto market_close = std::chrono::hours{16}; // 4:00 PM ET
    constexpr auto eod_cutoff = market_close - std::chrono::minutes{3}; // Stop trading and start liquidation at 3:57 PM ET

    auto is_market_hours = current_time >= market_open and current_time < eod_cutoff;
    auto force_eod_close = current_time >= eod_cutoff and current_time < market_close;

    // Show market hours status
#ifdef HAVE_FTXUI
    if (monitor) {
      auto is_open = is_market_hours and not force_eod_close;
      std::string countdown;
      if (current_time < market_open) {
        auto mins_to_open =
            std::chrono::duration_cast<std::chrono::minutes>(market_open -
                                                              current_time)
                .count();
        countdown = std::format("Opens in {}h {}m", mins_to_open / 60,
                                mins_to_open % 60);
      } else if (force_eod_close) {
        countdown = "Liquidating positions";
      } else {
        auto mins_to_close =
            std::chrono::duration_cast<std::chrono::minutes>(market_close -
                                                              current_time)
                .count();
        countdown = std::format("Closes in {}h {}m", mins_to_close / 60,
                                mins_to_close % 60);
      }
      monitor->set_market_status(is_open, countdown);
    }
#else
    if (current_time < market_open or current_time >= market_close)
      std::println("{}⏸️  OUTSIDE MARKET HOURS (9:30 AM - 4:00 PM ET) - No new trades{}\n", colour_yellow, colour_reset);
    else if (force_eod_close)
      std::println("{}🔔 END OF DAY LIQUIDATION (3:57 PM ET) - Closing all positions{}\n", colour_yellow, colour_reset);
#endif

    // Display account status
#ifdef HAVE_FTXUI
    print_account_stats(client, now, starting_equity, starting_equity_captured, monitor, tui_active);
#else
    print_account_stats(client, now, starting_equity, starting_equity_captured);
#endif

    // Fetch open positions and pending orders (API is source of truth)
    auto positions_result = client.get_positions();
    auto orders_result = client.get_open_orders();

    // Track symbols with open positions or pending orders
    auto symbols_in_use = std::set<std::string>{};

    if (positions_result) {
      auto positions_json =
          nlohmann::json::parse(positions_result.value(), nullptr, false);

      if (not positions_json.is_discarded()) {
        // Extract symbol names from API positions (API is source of truth)
        api_positions.clear();
        for (const auto &pos : positions_json) {
          auto symbol = pos["symbol"].get<std::string>();
          api_positions.insert(symbol);
          symbols_in_use.insert(symbol);
        }

        if (not positions_json.empty()) {
#ifdef HAVE_FTXUI
          // Only suppress console output if TUI is actually running
          if (not tui_active) {
#endif
            std::println("\n📊 OPEN POSITIONS");
            std::println("{:-<130}", "");
            std::println(
                "{:<10} {:>18} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10}",
                "SYMBOL", "QTY", "ENTRY", "CURRENT", "TARGET", "VALUE", "P&L",
                "P&L %", "TGT %");
            std::println("{:-<130}", "");
#ifdef HAVE_FTXUI
          }
#endif

          // Convert to vector and sort by P&L % (descending)
          auto positions_vec = std::vector<nlohmann::json>{};
          for (const auto &pos : positions_json)
            positions_vec.push_back(pos);

          std::ranges::sort(positions_vec, [](const auto &a, const auto &b) {
            auto entry_a = std::stod(a["avg_entry_price"].template get<std::string>());
            auto current_a = std::stod(a["current_price"].template get<std::string>());
            auto plpc_a = ((current_a - entry_a) / entry_a) * 100.0;

            auto entry_b = std::stod(b["avg_entry_price"].template get<std::string>());
            auto current_b = std::stod(b["current_price"].template get<std::string>());
            auto plpc_b = ((current_b - entry_b) / entry_b) * 100.0;

            return plpc_a > plpc_b; // Descending order (best first)
          });

          for (const auto &pos : positions_vec) {
            auto symbol = pos["symbol"].get<std::string>();
            auto qty = pos["qty"].get<std::string>();
            auto qty_double = std::stod(qty);
            auto avg_entry =
                std::stod(pos["avg_entry_price"].get<std::string>());
            auto current = std::stod(pos["current_price"].get<std::string>());
            auto cost_basis = std::stod(pos["cost_basis"].get<std::string>());

            // Calculate P&L ourselves from current price
            auto unrealized_plpc = ((current - avg_entry) / avg_entry) * 100.0;
            auto market_value = cost_basis * (1.0 + unrealized_plpc / 100.0);
            auto unrealized_pl = market_value - cost_basis;

            // Calculate target: use peak with trailing stop if profitable,
            // otherwise 2% TP
            auto target_price = avg_entry * 1.02;
            if (position_peaks.contains(symbol)) {
              auto peak = position_peaks[symbol];
              auto trailing_target = peak * (1.0 - trailing_stop_pct);
              if (trailing_target > target_price)
                target_price = trailing_target;
            }

            // Calculate target percentage from entry
            auto target_plpc = ((target_price - avg_entry) / avg_entry) * 100.0;

            auto colour = unrealized_plpc >= 0.0 ? colour_green : colour_red;

#ifdef HAVE_FTXUI
            // Update TUI with position data
            if (tui_active and monitor) {
              auto strategy = position_strategies.contains(symbol)
                                  ? position_strategies[symbol]
                                  : "unknown";
              auto entry_time = position_entry_times.contains(symbol)
                                    ? position_entry_times[symbol]
                                    : std::chrono::system_clock::now();

              monitor->update_position(
                  symbol, tui::position_data{
                              .symbol = symbol,
                              .strategy = strategy,
                              .entry_price = avg_entry,
                              .current_price = current,
                              .quantity = qty_double,
                              .market_value = market_value,
                              .unrealised_pnl = unrealized_pl,
                              .unrealised_pct = unrealized_plpc,
                              .take_profit_pct = take_profit_pct * 100.0,
                              .stop_loss_pct = stop_loss_pct * 100.0,
                              .trailing_stop_pct = trailing_stop_pct * 100.0,
                              .entry_time = entry_time,
                          });
            } else {
#endif
              std::println(
                  "{}{:<10} {:>18} {:>10.2f} {:>10.2f} {:>10.2f} {:>10.2f} "
                  "{:>10.2f} {:>9.2f}% {:>9.2f}%{}",
                  colour, symbol, qty, avg_entry, current, target_price,
                  market_value, unrealized_pl, unrealized_plpc, target_plpc,
                  colour_reset);
#ifdef HAVE_FTXUI
            }
#endif
          }
#ifdef HAVE_FTXUI
          if (not tui_active) {
#endif
            std::println("");
#ifdef HAVE_FTXUI
          }
#endif
        } else {
          // No open positions
#ifdef HAVE_FTXUI
          if (not tui_active) {
#endif
            std::println("\n{}📭 No open positions{}", colour_cyan, colour_reset);
#ifdef HAVE_FTXUI
          }
#endif
        }

        // Check if we need to close all positions before market close
        if (not positions_json.empty() and force_eod_close) {
          std::println("\n{}⏰ END OF DAY: Closing all positions (market closes in 5 minutes){}",
                       colour_yellow, colour_reset);

          for (const auto &pos : positions_json) {
            auto symbol = pos["symbol"].get<std::string>();
            auto unrealized_pl =
                std::stod(pos["unrealized_pl"].get<std::string>());
            auto cost_basis = std::stod(pos["cost_basis"].get<std::string>());
            auto profit_percent = (unrealized_pl / cost_basis) * 100.0;

            // Skip crypto - trades 24/7
            if (is_crypto(symbol)) {
              std::println("   Skipping {} (crypto trades 24/7)", symbol);
              continue;
            }

            std::println("   Closing {}: ${:.2f} ({:.2f}%)", symbol,
                         unrealized_pl, profit_percent);

            auto close_result = client.close_position(symbol);
            if (close_result) {
              std::println("   ✅ {} closed", symbol);

              auto strategy = position_strategies.contains(symbol)
                                  ? position_strategies[symbol]
                                  : std::string{"unknown"};

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
              std::println("   ❌ Failed to close {}", symbol);
            }
          }
          std::println("");
        }

        // Evaluate exits using strategy-specific parameters
        if (not positions_json.empty()) {
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

              std::println("{} {}: {} ${:.2f} ({:.2f}%)",
                           unrealized_pl > 0.0 ? "💰" : "🛑", exit_reason,
                           symbol, unrealized_pl, profit_percent);
              std::println("   Closing position...");

              auto close_result = client.close_position(symbol);
              if (close_result) {
                std::println("✅ Position closed: {}", symbol);

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

    // Parse open orders and add their symbols to symbols_in_use
    if (orders_result) {
      auto orders_json =
          nlohmann::json::parse(orders_result.value(), nullptr, false);

      if (not orders_json.is_discarded() and orders_json.is_array()) {
        for (const auto &order : orders_json) {
          auto symbol = order["symbol"].get<std::string>();
          symbols_in_use.insert(symbol);
        }

        if (not orders_json.empty()) {
          std::println("\n⏳ PENDING ORDERS: {}", orders_json.size());
          for (const auto &order : orders_json) {
            auto symbol = order["symbol"].get<std::string>();
            auto side = order["side"].get<std::string>();
            auto status = order["status"].get<std::string>();
            std::println("   {} {} ({})", symbol, side, status);
          }
        }
      }
    }

    // Clean up position_strategies: remove entries that don't have actual positions
    // BUT keep recent entries to prevent duplicate orders during API lag
    //
    // CRITICAL BUG PREVENTION: Protection against duplicate orders due to API/timing issues.
    // Observed behaviour: 3 ETH/USD orders placed 58-59 seconds apart created simultaneous positions.
    // Root cause unknown - could be:
    //   - Alpaca API lag in /v2/positions endpoint
    //   - Race condition in our order/position tracking
    //   - Network timing issues
    //   - Something else we haven't identified yet
    //
    // During this lag window, if we erase from position_strategies, the symbol appears
    // "available" and we place duplicate orders. This created 3 simultaneous ETH/USD
    // positions on 2026-01-13 at 15:11:41, 15:12:39, and 15:13:38 (58-59 seconds apart).
    //
    // SOLUTION: Keep entries in position_strategies for 5 minutes after entry, regardless
    // of API state. This prevents rapid duplicate orders while still cleaning up stale
    // entries from rejected/canceled orders eventually.
    //
    // TODO: Consider these improvements:
    //   - Increase to 30-60 minute window for stronger protection
    //   - Persist position_strategies to disk (survive bot restarts)
    //   - Add "max trades per symbol per session" hard limit
    //   - Track all orders in SQLite database for audit trail
    auto five_minutes_ago = now - std::chrono::minutes{5};
    for (auto it = position_strategies.begin(); it != position_strategies.end();) {
      auto symbol = it->first;
      auto recently_entered = position_entry_times.contains(symbol) and
                             position_entry_times[symbol] > five_minutes_ago;

      // Only erase if position doesn't exist AND wasn't recently entered
      if (not api_positions.contains(symbol) and not recently_entered) {
        position_peaks.erase(symbol);
        position_entry_times.erase(symbol);
        position_order_ids.erase(symbol);
        it = position_strategies.erase(it);
      } else {
        ++it;
      }
    }

    // Fetch market data - 15-minute bars for strategy signals
    // On first cycle, fetch last 20 bars (5 hours). After that, only fetch latest 2 bars.
    auto is_first_cycle = price_histories.empty();
    auto bars_end = now;
    auto bars_start = is_first_cycle ? now - std::chrono::hours{5}
                                     : now - std::chrono::minutes{30};  // Just last 2 bars
    auto start_str = std::format("{:%Y-%m-%dT%H:%M:%SZ}", bars_start);
    auto end_str = std::format("{:%Y-%m-%dT%H:%M:%SZ}", bars_end);

    // Track last processed bar timestamp per symbol to avoid re-processing
    static auto last_bar_timestamps = std::map<std::string, std::string>{};

    // Fetch 15-minute bars for all symbols
    for (const auto &symbol : stocks) {
      auto bars = client.get_bars(symbol, "15Min", start_str, end_str);
      if (bars and not bars->empty()) {
        auto &history = price_histories[symbol];

        if (is_first_cycle) {
          // First cycle: build full history
          history = lft::PriceHistory{};
          for (const auto &bar : *bars) {
            history.add_bar(bar.close, bar.high, bar.low, bar.volume);
          }
          // Remember the last bar's timestamp
          last_bar_timestamps[symbol] = bars->back().timestamp;
        } else {
          // Subsequent cycles: only add bars we haven't seen before
          auto last_timestamp = last_bar_timestamps[symbol];
          for (const auto &bar : *bars) {
            if (bar.timestamp > last_timestamp) {
              history.add_bar(bar.close, bar.high, bar.low, bar.volume);
              last_bar_timestamps[symbol] = bar.timestamp;
            }
          }
        }
      }
    }

    // Fetch snapshots for spread checking and display
    auto stock_snapshots = client.get_snapshots(stocks);

    print_header();

    // Process stocks
    if (stock_snapshots) {
      for (const auto &symbol : stocks) {
        if (stock_snapshots->contains(symbol)) {
          auto &snap = stock_snapshots->at(symbol);
          auto &history = price_histories[symbol];

          print_snapshot(symbol, snap, history);

          // Entry logic - only for enabled strategies and during market hours
          if (not is_market_hours) {
            // Skip all new trades outside market hours (9:30 AM - 4:00 PM ET)
            continue;
          }

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

          // DUPLICATE ORDER PREVENTION: Two-layer guard
          //
          // Layer 1: symbols_in_use (populated from API each cycle)
          //   - Contains symbols from /v2/positions and /v2/orders?status=open
          //   - Protects against placing orders when API shows active positions/orders
          //
          // Layer 2: position_strategies (persistent in-memory map)
          //   - Contains symbols we've placed orders for in this session
          //   - Protected by 5-minute cleanup grace period (see lines 1280-1310)
          //   - Critical for handling whatever timing issue caused 3 simultaneous ETH/USD
          //     positions on 2026-01-13 (root cause still under investigation)
          //
          // Both checks required: API might have lag, but our local tracking is authoritative
          if (not symbols_in_use.contains(symbol) and not position_strategies.contains(symbol)) {
            // Execute first enabled signal
            for (const auto &signal : signals) {
              if (signal.should_buy and
                  configs.contains(signal.strategy_name) and
                  configs.at(signal.strategy_name).enabled) {

                // Check trade eligibility (spread and volume filters)
                auto crypto = is_crypto(symbol);
                auto max_spread = get_max_spread_bps(crypto);

                if (not lft::Strategies::is_tradeable(snap, history, max_spread,
                                                      min_volume_ratio)) {
                  auto spread_bps = lft::Strategies::calculate_spread_bps(snap);
                  auto vol_ratio =
                      lft::Strategies::calculate_volume_ratio(history);

                  std::println("{}⛔ TRADE BLOCKED: {}{}", colour_yellow,
                               symbol, colour_reset);
                  std::println("   Spread: {:.1f} bps (max {:.1f}), Volume: "
                               "{:.3f}x avg (min {:.1f}x)",
                               spread_bps, max_spread, vol_ratio,
                               min_volume_ratio);
                  std::println("   Quotes: bid={:.2f}, ask={:.2f}, mid={:.2f}",
                               snap.latest_quote_bid, snap.latest_quote_ask,
                               (snap.latest_quote_bid + snap.latest_quote_ask) / 2.0);
                  std::println("   Signal: {} - {}", signal.strategy_name,
                               signal.reason);

                  // Log blocked trade
                  log_blocked_trade(symbol, signal.strategy_name, signal.reason,
                                    spread_bps, max_spread, vol_ratio,
                                    min_volume_ratio, now);

                  break; // Skip this trade and move to next symbol
                }

                // Check if trade has positive edge after costs
                auto spread_bps = lft::Strategies::calculate_spread_bps(snap);
                auto total_cost_bps = calculate_total_cost_bps(spread_bps);

                // Use expected move from config (calibrated), or signal, or
                // default
                auto expected_move_bps =
                    configs.at(signal.strategy_name).expected_move_bps;
                if (expected_move_bps <= 0.0) {
                  expected_move_bps = signal.expected_move_bps > 0.0
                                          ? signal.expected_move_bps
                                          : total_cost_bps * 2.0;
                }

                if (not has_positive_edge(expected_move_bps, total_cost_bps)) {
                  auto net_edge_bps = expected_move_bps - total_cost_bps;
                  std::println("{}💸 TRADE BLOCKED: Insufficient edge{}",
                               colour_yellow, colour_reset);
                  std::println(
                      "   Expected move: {:.1f} bps, Total cost: {:.1f} bps "
                      "(spread {:.1f} + slippage {:.1f} + adverse {:.1f})",
                      expected_move_bps, total_cost_bps, spread_bps,
                      slippage_buffer_bps, adverse_selection_bps);
                  std::println(
                      "   Net edge: {:.1f} bps (min required: {:.1f} bps)",
                      net_edge_bps, min_edge_bps);
                  std::println("   Signal: {} - {}", signal.strategy_name,
                               signal.reason);

                  // Log cost-blocked trade (reuse blocked trades CSV)
                  auto vol_ratio =
                      lft::Strategies::calculate_volume_ratio(history);

                  log_blocked_trade(symbol, signal.strategy_name, signal.reason,
                                    spread_bps, max_spread, vol_ratio,
                                    min_volume_ratio, now);

                  break; // Skip this trade and move to next symbol
                }

                std::println("{}🚨 SIGNAL: {} - {}{}", colour_cyan,
                             signal.strategy_name, signal.reason, colour_reset);
                std::println(
                    "   Expected move: {:.1f} bps, Cost: {:.1f} bps, Net "
                    "edge: {:.1f} bps",
                    expected_move_bps, total_cost_bps,
                    expected_move_bps - total_cost_bps);
                std::println("   Buying ${:.0f} of {}...", notional_amount,
                             symbol);

                // Encode strategy and exit params in client_order_id (includes symbol and timestamp for uniqueness)
                auto client_order_id =
                    encode_client_order_id(signal.strategy_name, symbol,
                                           take_profit_pct, stop_loss_pct, trailing_stop_pct);
                auto order =
                    client.place_order(symbol, "buy", notional_amount, client_order_id);
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

                      // WITHIN-CYCLE PROTECTION: Immediately add to symbols_in_use
                      // If multiple strategies fire in the same cycle (before next API refresh),
                      // this prevents placing duplicate orders for the same symbol
                      symbols_in_use.insert(symbol);

                      ++strategy_stats[signal.strategy_name].trades_executed;

                    } else {
                      std::println("{}⚠  Order status '{}' - may not execute{}",
                                   colour_yellow, status, colour_reset);
                    }
                  } else {
                    std::println("✅ Order placed (could not parse response)");
                    position_strategies[symbol] = signal.strategy_name;
                    position_entry_times[symbol] = now;

                    // WITHIN-CYCLE PROTECTION: Immediately add to symbols_in_use
                    symbols_in_use.insert(symbol);

                    ++strategy_stats[signal.strategy_name].trades_executed;

                    // Can't log strategy assignment without order_id
                  }
                } else {
                  // Order failed - explain why and continue
                  std::println("{}❌ Order failed for {}: {}{}", colour_red,
                               symbol,
                               "likely insufficient buying power or "
                               "non-marginable security",
                               colour_reset);
                  std::println("{}   Continuing with other symbols...{}",
                               colour_yellow, colour_reset);
                }

                break; // One strategy per symbol
              }
            }
          } else {
            // Check if blocked by existing position
            auto it = std::ranges::find_if(signals, [&](const auto &signal) {
              return signal.should_buy and
                     configs.contains(signal.strategy_name) and
                     configs.at(signal.strategy_name).enabled;
            });
            if (it != signals.end())
              std::println(
                  "{}⏸  BLOCKED: {} signal for {} (position already open){}",
                  colour_yellow, it->strategy_name, symbol, colour_reset);
          }
        }
      }
    }

    // Crypto trading removed 2026-01-16
    // Reason: All crypto pairs disabled due to duplicate order bug (2026-01-13)
    // Simplifies codebase and eliminates code duplication between stock/crypto loops
    // Can restore from git history when crypto trading is re-enabled

    // Sleep until :35 past next minute (after Alpaca's :30 bar recalculation)
    auto next_update = std::chrono::ceil<std::chrono::minutes>(now) + 35s;
    auto cycles_remaining = max_cycles - cycle;

    std::println("\n⏳ Next update at {:%H:%M:%S} | {} cycles remaining\n",
                 next_update, cycles_remaining);
    std::this_thread::sleep_until(next_update);
  }
}

} // anonymous namespace

int main() {
  auto client = lft::AlpacaClient{};

  if (not client.is_valid()) {
    std::println("❌ ALPACA_API_KEY and ALPACA_API_SECRET must be set");
    return 1;
  }

#ifdef HAVE_FTXUI
  // Initialize TUI
  auto monitor = std::make_shared<tui::trading_monitor>();
  std::unique_ptr<tui::tui_renderer> renderer;

  if (tui::supports_interactive()) {
    renderer = std::make_unique<tui::tui_renderer>(monitor);
    renderer->start();
  } else {
    std::println("{}⚠ TUI not supported, using text mode{}", colour_yellow,
                 colour_reset);
  }
#else
  std::println("{}🤖 LFT - LOW FREQUENCY TRADER{}", colour_cyan, colour_reset);
#endif

  // Phase 1: Calibrate
  auto configs = calibrate_all_strategies(client, stocks, crypto);

  // Check if any strategies are enabled
  if (std::ranges::none_of(configs,
                           [](const auto &p) { return p.second.enabled; })) {
#ifdef HAVE_FTXUI
    if (not renderer)
#endif
      std::println("{}⚠ No profitable strategies - will only manage exits{}\n",
                   colour_yellow, colour_reset);
  }

  // Phase 2: Live trading (runs for 1 hour, then exits)
#ifdef HAVE_FTXUI
  run_live_trading(client, stocks, crypto, configs, monitor, renderer != nullptr);
#else
  run_live_trading(client, stocks, crypto, configs);
#endif

#ifdef HAVE_FTXUI
  if (renderer)
    renderer->stop();
#endif

  return 0;
}
