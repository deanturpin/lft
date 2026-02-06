// Market Evaluation Phase
// Fetches current market data and analyses entry signals without executing trades
// Runs every minute regardless of market hours

#include "lft.h"
#include "defs.h"
#include "strategies.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <map>
#include <thread>
#include <numeric>
#include <print>
#include <set>
#include <string>
#include <vector>

MarketEvaluation evaluate_market(AlpacaClient &client,
                                  const std::map<std::string, bool> &enabled_strategies,
                                  const std::set<std::string> &symbols_in_use) {
  auto result = MarketEvaluation{};

  // Build price histories for relative strength (if strategy is enabled)
  auto price_histories = std::map<std::string, PriceHistory>{};
  if (enabled_strategies.contains("relative_strength") and enabled_strategies.at("relative_strength")) {
    for (const auto &symbol : stocks) {
      if (auto bars = client.get_bars(symbol, "15Min", 100)) {
        auto &history = price_histories[symbol];
        for (const auto &bar : *bars) {
          history.add_bar(bar.close, bar.high, bar.low, bar.volume);
        }
        if (not bars->empty()) {
          history.last_price = bars->back().close;
          history.has_history = true;
        }
      }
    }
  }

  auto total_spread_bps = 0.0;
  auto count = 0uz;
  auto network_failed = false;

  // Evaluate each watchlist symbol
  for (const auto &symbol : stocks) {
    auto eval = SymbolEvaluation{};
    eval.symbol = symbol;

    // Track if we're in position (but continue to show market data)
    const auto in_position = symbols_in_use.contains(symbol);

    // If network already failed on a previous symbol, don't spam more errors
    if (network_failed) {
      eval.status_summary = "Network error";
      result.symbols.push_back(eval);
      continue;
    }

    // Fetch latest bar data and snapshot
    auto bars_opt = client.get_bars(symbol, "15Min", 100);
    auto snapshot_opt = client.get_snapshot(symbol);

    // Delay to avoid API rate limiting (100ms = max 600 req/min, well under limit)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (not bars_opt or not snapshot_opt) {
      eval.status_summary = "Data unavailable";
      result.symbols.push_back(eval);
      network_failed = true; // Stop trying other symbols to avoid error spam
      continue;
    }

    const auto &bars = *bars_opt;
    const auto &snapshot = *snapshot_opt;

    eval.price = snapshot.latest_trade_price;

    // Calculate daily change percentage for market breadth
    if (snapshot.prev_daily_bar_close > 0.0) {
      eval.daily_change_pct = ((snapshot.latest_trade_price - snapshot.prev_daily_bar_close) /
                                snapshot.prev_daily_bar_close) * 100.0;
    }

    // Calculate spread
    if (snapshot.latest_quote_bid > 0.0 and snapshot.latest_quote_ask > 0.0) {
      eval.spread_bps = ((snapshot.latest_quote_ask - snapshot.latest_quote_bid) /
                         snapshot.latest_quote_bid) * 10000.0;
      total_spread_bps += eval.spread_bps;
      ++count;
    }

    // Calculate volume ratio (current vs 20-bar average)
    if (bars.size() >= 20) {
      auto total_volume = 0.0;
      for (auto i = bars.size() - 20; i < bars.size(); ++i)
        total_volume += bars[i].volume;
      const auto avg_volume = total_volume / 20.0;
      const auto current_volume = bars.back().volume;
      eval.volume_ratio = avg_volume > 0.0 ? current_volume / avg_volume : 0.0;
    }

    // Calculate total trading costs
    const auto total_costs_bps = eval.spread_bps + slippage_buffer_bps + adverse_selection_bps;

    // Edge shows remaining profit potential: min required edge minus all costs
    // Negative edge means costs exceed minimum required edge (unprofitable)
    eval.edge_bps = min_edge_bps - total_costs_bps;

    // Check tradeability based on spread and volume
    const auto spread_ok = eval.spread_bps > 0.0 and eval.spread_bps <= max_spread_bps_stocks;
    const auto volume_ok = eval.volume_ratio >= min_volume_ratio;
    eval.tradeable = spread_ok and volume_ok;

    // Build price history from bars (always evaluate strategies, even if spread/volume issues)
    auto history = PriceHistory{};
    for (const auto &bar : bars) {
      history.add_bar(bar.close, bar.high, bar.low, bar.volume);
    }
    history.last_price = snapshot.latest_trade_price;
    history.has_history = not bars.empty();

    // Evaluate all strategies
    const auto all_signals = std::vector<StrategySignal>{
        Strategies::evaluate_ma_crossover(history),
        Strategies::evaluate_mean_reversion(history),
        Strategies::evaluate_volatility_breakout(history),
        Strategies::evaluate_relative_strength(history, price_histories),
        Strategies::evaluate_volume_surge(history)
    };

    // Check each enabled strategy
    for (const auto &[strategy_name, is_enabled] : enabled_strategies) {
      if (not is_enabled) {
        eval.strategy_signals[strategy_name] = false;
        continue;
      }

      // Check if this strategy fired
      const auto has_signal = std::ranges::any_of(all_signals, [&](const auto &sig) {
        return sig.should_buy and sig.strategy_name == strategy_name;
      });

      eval.strategy_signals[strategy_name] = has_signal;
      if (has_signal)
        ++result.total_signals;
    }

    // Build status summary and ready-to-trade flag
    const auto signal_count = std::ranges::count_if(eval.strategy_signals, [](const auto &p) {
      return p.second;
    });

    eval.ready_to_trade = eval.tradeable and signal_count > 0 and not in_position;

    // Build comprehensive status showing all blocking issues
    if (in_position) {
      eval.status_summary = "In position";
    } else if (not eval.tradeable) {
      auto issues = std::vector<std::string>{};

      if (eval.spread_bps == 0.0)
        issues.push_back("No quote");
      else if (eval.spread_bps > max_spread_bps_stocks)
        issues.push_back(std::format("Spread {:.0f}bps", eval.spread_bps));

      if (eval.volume_ratio > 0.0 and eval.volume_ratio < min_volume_ratio)
        issues.push_back(std::format("Vol {:.2f}x", eval.volume_ratio));

      eval.status_summary = issues.empty() ? "Not tradeable" :
        std::accumulate(std::next(issues.begin()), issues.end(), issues[0],
                       [](const auto& a, const auto& b) { return a + " + " + b; });
    } else if (signal_count > 0) {
      eval.status_summary = std::format("{} signals", signal_count);
    } else {
      eval.status_summary = "No signals";
    }

    result.symbols.push_back(eval);
  }

  result.tradeable_count = std::ranges::count_if(result.symbols, [](const auto &s) {
    return s.tradeable;
  });

  result.avg_spread_bps = count > 0 ? total_spread_bps / static_cast<double>(count) : 0.0;

  return result;
}

void display_evaluation(const MarketEvaluation &eval,
                       const std::map<std::string, bool> &enabled_strategies,
                       std::chrono::system_clock::time_point now) {
  // Calculate market breadth
  const auto advancing = std::ranges::count_if(eval.symbols, [](const auto &s) {
    return s.daily_change_pct > 0.0;
  });

  std::println("\n📥 Checking entries at {:%H:%M:%S}",
               std::chrono::floor<std::chrono::seconds>(now));
  std::println("  Tradeable symbols: {}/{}", eval.tradeable_count, eval.symbols.size());
  std::println("  Average spread:    {:.1f} bps", eval.avg_spread_bps);
  std::println("  Active signals:    {}", eval.total_signals);
  std::println("  Market breadth:    {}/{} advancing", advancing, eval.symbols.size());

  // Build strategy name list for header
  auto strategy_names = std::vector<std::string>{};
  for (const auto &[name, enabled] : enabled_strategies)
    if (enabled)
      strategy_names.push_back(name);

  std::println("\n  Symbol   Price    Spread  Edge   Vol    Strategies  Ready  Status");
  std::println("                     (bps)   (bps)  Ratio");
  std::println("  ────────────────────────────────────────────────────────────────────────────");

  for (const auto &s : eval.symbols) {
    // Build strategy indicator string (✓ or ✗ for each strategy)
    auto strategy_str = std::string{};
    for (const auto &name : strategy_names) {
      if (s.strategy_signals.contains(name)) {
        strategy_str += s.strategy_signals.at(name) ? "✓" : "✗";
      } else {
        strategy_str += "-";
      }
      strategy_str += " ";
    }

    const auto ready_indicator = s.ready_to_trade ? "✓" : " ";

    std::println("  {:7} ${:7.2f}  {:>6.0f}  {:>6.0f}  {:>5.2f}  {:11} {:5}  {}",
                 s.symbol, s.price, s.spread_bps, s.edge_bps, s.volume_ratio, strategy_str, ready_indicator, s.status_summary);
  }
}
