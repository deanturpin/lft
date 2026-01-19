// Phase 1: Strategy Calibration
// Backtests all strategies on historical data and enables profitable ones

#include "defs.h"
#include "lft.h"
#include "strategies.h"
#include <algorithm>
#include <map>
#include <print>
#include <string>
#include <string_view>
#include <vector>

// Exit parameters (from exit_logic_tests.h concept)
constexpr auto take_profit_pct = 0.02;   // 2%
constexpr auto stop_loss_pct = 0.05;     // 5%
constexpr auto trailing_stop_pct = 0.30; // 30%
constexpr auto notional_per_trade = 1000.0;

// Compile-time validation of exit parameters
static_assert(take_profit_pct > 0.0, "Take profit must be positive");
static_assert(take_profit_pct >= 0.01, "Take profit too small - min 1%");
static_assert(take_profit_pct <= 0.10, "Take profit too large - max 10%");
static_assert(stop_loss_pct > 0.0, "Stop loss must be positive");
static_assert(stop_loss_pct >= 0.01, "Stop loss too small - min 1%");
static_assert(stop_loss_pct <= 0.20, "Stop loss too large - max 20%");
static_assert(stop_loss_pct >= take_profit_pct,
              "Stop loss should be >= take profit (risk management)");
static_assert(trailing_stop_pct > 0.0, "Trailing stop must be positive");
static_assert(trailing_stop_pct >= 0.05, "Trailing stop too tight - min 5%");
static_assert(trailing_stop_pct <= 0.50, "Trailing stop too loose - max 50%");
static_assert(trailing_stop_pct >= take_profit_pct,
              "Trailing stop should be >= take profit");
static_assert(notional_per_trade > 0.0, "Trade size must be positive");
static_assert(notional_per_trade >= 100.0, "Trade size too small - min $100");
static_assert(notional_per_trade <= 10000.0,
              "Trade size dangerously high - max $10k per trade");

namespace {

// Run backtest for a single strategy across all symbols
StrategyStats run_backtest_for_strategy(
    std::string_view strategy_name,
    const std::map<std::string, std::vector<Bar>> &all_bars,
    double starting_capital) {

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
        const auto pl_dollars =
            (current_price - pos.entry_price) * pos.quantity;
        const auto pl_pct = pl_dollars / (pos.entry_price * pos.quantity);

        // Update peak for trailing stop
        if (current_price > pos.peak_price)
          pos.peak_price = current_price;

        // Check exit conditions
        const auto should_exit =
            (pl_pct >= take_profit_pct) or // Take profit
            (pl_pct <= -stop_loss_pct) or  // Stop loss
            (current_price <
             pos.peak_price * (1.0 - trailing_stop_pct)); // Trailing stop

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
          signal =
              Strategies::evaluate_relative_strength(history, all_histories);
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

} // anonymous namespace

std::map<std::string, bool>
calibrate(const std::map<std::string, std::vector<Bar>> &all_bars,
          double starting_capital) {
  auto enabled = std::map<std::string, bool>{};
  auto strategy_stats = std::map<std::string, StrategyStats>{};

  std::println("\n  Using starting capital: ${:.2f}", starting_capital);

  // Run backtest for each strategy
  std::println("");

  const auto strategies = std::vector<std::string>{
      "ma_crossover", "mean_reversion", "volatility_breakout",
      "relative_strength", "volume_surge"};

  for (const auto &strategy : strategies) {
    std::println("  🔧 Testing {}...", strategy);

    // Run actual backtest
    auto stats = run_backtest_for_strategy(strategy, all_bars, starting_capital);
    strategy_stats[strategy] = stats;

    std::println("     ✓ Complete - {} trades, ${:.2f} P&L",
                 stats.trades_closed, stats.net_profit());

    // Enable if profitable AND has sufficient trade history
    enabled[strategy] = (stats.net_profit() > 0.0 and
                         stats.trades_closed >= min_trades_to_enable);
  }

  // Print summary table
  std::println("\n📊 Calibration complete:");
  auto enabled_count = 0uz;
  for (const auto &strategy : strategies) {
    const auto &stats = strategy_stats[strategy];
    const auto is_enabled = enabled[strategy];
    const auto status = is_enabled ? "ENABLED " : "DISABLED";

    std::println("  {:<20} {:>10} P&L=${:>8.2f} WR={:>5.1f}%", strategy, status,
                 stats.net_profit(), stats.win_rate());

    if (is_enabled)
      ++enabled_count;
  }

  std::println("\n  {} of {} strategies enabled for live trading\n",
               enabled_count, strategies.size());

  return enabled;
}
