#include "shared/alpaca_client.h"
#include "shared/strategies.h"
#include <chrono>
#include <format>
#include <iostream>
#include <map>
#include <print>
#include <set>

using namespace std::chrono_literals;

namespace {

// ANSI colour codes
constexpr auto colour_reset = "\033[0m";
constexpr auto colour_green = "\033[32m";
constexpr auto colour_red = "\033[31m";
constexpr auto colour_cyan = "\033[36m";
constexpr auto colour_yellow = "\033[33m";

// Trading parameters (same as live ticker)
constexpr auto dip_threshold = -0.2;
constexpr auto notional_amount = 50.0;
constexpr auto take_profit_threshold = 1.0; // Minimum $1 profit
constexpr auto stop_loss_amount = -5.0;     // -$5 loss
constexpr auto take_profit_pct = 0.01;      // 1% profit
constexpr auto stop_loss_pct = -0.05;       // -5% loss
constexpr auto trailing_stop_pct = 0.02;    // Trail by 2% from peak

// Position tracking
struct Position {
  std::string symbol;
  std::string strategy;
  double entry_price{};
  double quantity{};
  std::string entry_time;
  double peak_price{}; // Track highest price for trailing stop
};

struct BacktestStats {
  std::map<std::string, lft::StrategyStats> strategy_stats;
  double cash{10000.0}; // Start with $10k
  double initial_cash{10000.0};
  std::map<std::string, Position> positions;
  int total_trades{};
  int winning_trades{};
  int losing_trades{};
};

void print_summary(const BacktestStats &stats) {
  std::println("\n{}📊 BACKTEST RESULTS{}", colour_cyan, colour_reset);
  std::println("{:-<100}", "");

  auto total_pl = stats.cash - stats.initial_cash;
  auto pl_colour = total_pl >= 0.0 ? colour_green : colour_red;
  auto return_pct = (total_pl / stats.initial_cash) * 100.0;

  std::println("Initial Capital: ${:.2f}", stats.initial_cash);
  std::println("Final Capital:   {}{:.2f}{} ({:+.2f}%)", pl_colour, stats.cash,
               colour_reset, return_pct);
  std::println("Total P&L:       {}{:+.2f}{}", pl_colour, total_pl,
               colour_reset);
  std::println("Total Trades:    {}", stats.total_trades);
  std::println("Win Rate:        {:.1f}% ({}/{} wins)",
               stats.total_trades > 0
                   ? (stats.winning_trades * 100.0 / stats.total_trades)
                   : 0.0,
               stats.winning_trades, stats.total_trades);

  std::println("\n{:-<110}", "");
  std::println("{:<18} {:>10} {:>10} {:>10} {:>10} {:>12} {:>12} {:>12}",
               "STRATEGY", "SIGNALS", "EXECUTED", "CLOSED", "WINS", "WIN RATE",
               "NET P&L", "AVG P&L");
  std::println("{:-<110}", "");

  for (const auto &[name, strategy_stat] : stats.strategy_stats) {
    auto colour = strategy_stat.net_profit() >= 0.0 ? colour_green : colour_red;
    auto avg_pl = strategy_stat.trades_closed > 0
                      ? strategy_stat.net_profit() / strategy_stat.trades_closed
                      : 0.0;

    std::println(
        "{}{:<18} {:>10} {:>10} {:>10} {:>10} {:>11.1f}% {:>11.2f} {:>11.2f}{}",
        colour, strategy_stat.name, strategy_stat.signals_generated,
        strategy_stat.trades_executed, strategy_stat.trades_closed,
        strategy_stat.profitable_trades, strategy_stat.win_rate(),
        strategy_stat.net_profit(), avg_pl, colour_reset);
  }
  std::println("");
}

void process_bar(const std::string &symbol, const lft::Bar &bar,
                 lft::PriceHistory &history,
                 const std::map<std::string, lft::PriceHistory> &all_histories,
                 BacktestStats &stats) {

  // Update price history with bar close price
  history.add_price(bar.close);

  // Check if we have an open position for this symbol
  auto has_position = stats.positions.contains(symbol);

  if (has_position) {
    // Evaluate exit conditions
    auto &pos = stats.positions[symbol];
    auto current_value = pos.quantity * bar.close;
    auto cost_basis = pos.quantity * pos.entry_price;
    auto unrealized_pl = current_value - cost_basis;
    auto pl_pct = (bar.close - pos.entry_price) / pos.entry_price;

    // Update peak price for trailing stop
    if (bar.close > pos.peak_price)
      pos.peak_price = bar.close;

    // Calculate trailing stop trigger
    auto trailing_stop_price = pos.peak_price * (1.0 + trailing_stop_pct);
    auto trailing_stop_triggered = bar.close < trailing_stop_price;

    // Exit conditions: dollar-based OR percentage-based OR trailing stop
    auto should_exit =
        unrealized_pl >= take_profit_threshold or // Dollar profit target
        pl_pct >= take_profit_pct or              // Percentage profit target
        unrealized_pl <= stop_loss_amount or      // Dollar stop loss
        pl_pct <= stop_loss_pct or                // Percentage stop loss
        trailing_stop_triggered;                  // Trailing stop

    if (should_exit) {
      // Close position
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
        lft::Strategies::evaluate_dip(history, dip_threshold),
        lft::Strategies::evaluate_ma_crossover(history),
        lft::Strategies::evaluate_mean_reversion(history),
        lft::Strategies::evaluate_volatility_breakout(history),
        lft::Strategies::evaluate_relative_strength(history, all_histories)};

    // Count signals generated
    for (const auto &signal : signals) {
      if (signal.should_buy)
        ++stats.strategy_stats[signal.strategy_name].signals_generated;
    }

    // Execute first signal that fires
    for (const auto &signal : signals) {
      if (signal.should_buy and stats.cash >= notional_amount) {
        // Open position
        auto quantity = notional_amount / bar.close;
        auto actual_cost = quantity * bar.close;

        stats.cash -= actual_cost;
        stats.positions[symbol] = Position{
            .symbol = symbol,
            .strategy = signal.strategy_name,
            .entry_price = bar.close,
            .quantity = quantity,
            .entry_time = bar.timestamp,
            .peak_price = bar.close // Initialise peak to entry price
        };

        ++stats.strategy_stats[signal.strategy_name].trades_executed;
        ++stats.total_trades;

        // Only one strategy per symbol
        break;
      }
    }
  }
}

} // anonymous namespace

int main() {
  auto client = lft::AlpacaClient{};

  if (not client.is_valid()) {
    std::println("❌ ALPACA_API_KEY and ALPACA_API_SECRET must be set");
    return 1;
  }

  std::println("{}🔬 LFT BACKTESTING ENGINE{}", colour_cyan, colour_reset);
  std::println("Testing multi-strategy system on historic data\n");

  // Same watchlist as live ticker
  auto stocks = std::vector<std::string>{"AAPL",  "TSLA", "NVDA", "MSFT",
                                         "GOOGL", "AMZN", "META"};
  auto crypto = std::vector<std::string>{"BTC/USD", "ETH/USD"};

  // Backtest period: last 30 days of 1-minute bars
  auto now = std::chrono::system_clock::now();
  auto thirty_days_ago = now - std::chrono::hours(24 * 30);

  auto start = std::format("{:%Y-%m-%dT%H:%M:%SZ}", thirty_days_ago);
  auto end = std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);

  std::println("Period: {} to {}", start, end);
  std::println("Timeframe: 1 minute bars");
  std::println("Initial capital: $10,000\n");

  // Initialise backtest stats
  auto stats = BacktestStats{};
  stats.strategy_stats["dip"] = lft::StrategyStats{"dip"};
  stats.strategy_stats["ma_crossover"] = lft::StrategyStats{"ma_crossover"};
  stats.strategy_stats["mean_reversion"] = lft::StrategyStats{"mean_reversion"};
  stats.strategy_stats["volatility_breakout"] =
      lft::StrategyStats{"volatility_breakout"};
  stats.strategy_stats["relative_strength"] =
      lft::StrategyStats{"relative_strength"};

  // Price histories for all symbols
  auto price_histories = std::map<std::string, lft::PriceHistory>{};

  // Fetch and process historic bars for each symbol
  std::println("Fetching historic data...\n");

  // Combine all symbols for processing
  auto all_symbols = stocks;
  all_symbols.insert(all_symbols.end(), crypto.begin(), crypto.end());

  // Map symbol to bars
  auto symbol_bars = std::map<std::string, std::vector<lft::Bar>>{};

  for (const auto &symbol : all_symbols) {
    auto is_crypto = symbol.find('/') != std::string::npos;

    std::println("Fetching {} bars...", symbol);

    auto bars = is_crypto ? client.get_crypto_bars(symbol, "1Min", start, end)
                          : client.get_bars(symbol, "1Min", start, end);

    if (not bars) {
      std::println("{}⚠ Failed to fetch {} bars{}", colour_yellow, symbol,
                   colour_reset);
      continue;
    }

    std::println("  {} bars fetched", bars->size());
    symbol_bars[symbol] = std::move(*bars);
  }

  // Find the maximum number of bars across all symbols for iteration
  auto max_bars = 0uz;
  for (const auto &[sym, bars] : symbol_bars)
    max_bars = std::max(max_bars, bars.size());

  std::println("\nSimulating {} time periods...", max_bars);

  // Iterate through time, processing each symbol's bar at each timestamp
  for (auto i = 0uz; i < max_bars; ++i) {
    // Process each symbol's bar at this time index
    for (const auto &symbol : all_symbols) {
      if (not symbol_bars.contains(symbol))
        continue;

      const auto &bars = symbol_bars[symbol];
      if (i >= bars.size())
        continue;

      const auto &bar = bars[i];
      auto &history = price_histories[symbol];

      process_bar(symbol, bar, history, price_histories, stats);
    }

    // Progress indicator every 1000 bars
    if ((i + 1) % 1000 == 0)
      std::println("  Processed {} / {} periods...", i + 1, max_bars);
  }

  // Close any remaining positions at final prices
  for (const auto &[symbol, pos] : stats.positions) {
    if (not symbol_bars.contains(symbol))
      continue;

    const auto &bars = symbol_bars[symbol];
    if (bars.empty())
      continue;

    auto final_price = bars.back().close;
    auto current_value = pos.quantity * final_price;
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

  // Print results
  print_summary(stats);

  return 0;
}
