#include "shared/alpaca_client.h"
#include "shared/strategies.h"
#include <chrono>
#include <format>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <print>
#include <set>
#include <thread>

using namespace std::chrono_literals;
using json = nlohmann::json;

namespace {

// ANSI colour codes for terminal output
constexpr auto colour_reset = "\033[0m";
constexpr auto colour_green = "\033[32m";
constexpr auto colour_red = "\033[31m";
constexpr auto colour_yellow = "\033[33m";
constexpr auto colour_cyan = "\033[36m";

constexpr auto alert_threshold = 2.0;   // Alert on >2% change
constexpr auto dip_threshold = -0.2;    // Buy on >0.2% drop
constexpr auto notional_amount = 100.0; // Buy $100 worth
constexpr auto take_profit_threshold = 1.0;    // Minimum $1 profit
constexpr auto stop_loss_amount = -5.0;        // -$5 loss
constexpr auto take_profit_pct = 0.01;         // 1% profit
constexpr auto stop_loss_pct = -0.05;          // -5% loss
constexpr auto trailing_stop_pct = 0.02;       // Trail by 2% from peak

// Track which strategy opened each position
std::map<std::string, std::string> position_strategies;

// Track peak prices for trailing stops
std::map<std::string, double> position_peaks;

// Strategy performance tracking
std::map<std::string, lft::StrategyStats> strategy_stats;

void print_header() {
  std::println("\n{:<10} {:>12} {:>12} {:>12} {:>10} {}", "SYMBOL", "LAST",
               "BID", "ASK", "CHANGE%", "STATUS");
  std::println("{:-<70}", "");
}

void print_snapshot(const std::string &symbol, const lft::Snapshot &snap,
                    lft::PriceHistory &history) {
  auto status = std::string{};
  auto colour = colour_reset;

  history.add_price(snap.latest_trade_price);

  if (history.has_history) {
    // Choose colour based on price movement
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

void print_strategy_stats() {
  std::println("\n📊 STRATEGY PERFORMANCE");
  std::println("{:-<110}", "");
  std::println("{:<18} {:>10} {:>10} {:>10} {:>10} {:>12} {:>12} {:>12}",
               "STRATEGY", "SIGNALS", "EXECUTED", "CLOSED", "WINS", "WIN RATE",
               "NET P&L", "AVG P&L");
  std::println("{:-<110}", "");

  for (const auto &[name, stats] : strategy_stats) {
    auto colour = stats.net_profit() >= 0.0 ? colour_green : colour_red;
    auto avg_pl = stats.trades_closed > 0
                      ? stats.net_profit() / stats.trades_closed
                      : 0.0;

    std::println(
        "{}{:<18} {:>10} {:>10} {:>10} {:>10} {:>11.1f}% {:>11.2f} {:>11.2f}{}",
        colour, stats.name, stats.signals_generated, stats.trades_executed,
        stats.trades_closed, stats.profitable_trades, stats.win_rate(),
        stats.net_profit(), avg_pl, colour_reset);
  }
  std::println("");
}

} // anonymous namespace

int main(int argc, char *argv[]) {
  // Check for strategies flag
  auto strategies_enabled = false;
  if (argc > 1 and std::string{argv[1]} == "--strategies")
    strategies_enabled = true;

  try {
    auto client = lft::AlpacaClient{};

    // Check credentials are set
    if (not client.is_valid()) {
      std::println("❌ ALPACA_API_KEY and ALPACA_API_SECRET must be set");
      return 1;
    }

    // Test authentication and show account info
    std::println("Testing Alpaca connection...");
    auto account = client.get_account();
    if (not account) {
      std::println("❌ Failed to authenticate with Alpaca");
      return 1;
    }

    // Parse and display basic account info
    try {
      auto account_json = json::parse(account.value());
      auto buying_power = account_json["buying_power"].get<std::string>();
      auto cash = account_json["cash"].get<std::string>();
      std::println("✅ Connected to Alpaca (paper trading)");
      std::println("💰 Buying power: ${}", buying_power);
      std::println("💵 Cash: ${}\n", cash);
    } catch (...) {
      std::println("✅ Connected to Alpaca (paper trading)\n");
    }

    // Watchlists by asset type
    auto stocks = std::vector<std::string>{
        "AAPL", "TSLA", "NVDA", "MSFT", "GOOGL", "AMZN", "META", "RR.L", "EZJ"};
    auto crypto =
        std::vector<std::string>{"BTC/USD", "ETH/USD", "SOL/USD", "DOGE/USD"};
    auto price_history = std::map<std::string, lft::PriceHistory>{};

    // Initialise strategy stats
    strategy_stats["dip"] = lft::StrategyStats{"dip"};
    strategy_stats["ma_crossover"] = lft::StrategyStats{"ma_crossover"};
    strategy_stats["mean_reversion"] = lft::StrategyStats{"mean_reversion"};
    strategy_stats["volatility_breakout"] =
        lft::StrategyStats{"volatility_breakout"};
    strategy_stats["relative_strength"] =
        lft::StrategyStats{"relative_strength"};

    if (strategies_enabled) {
      std::println("{}📈 Multi-Strategy Trading ENABLED{}", colour_cyan,
                   colour_reset);
      std::println("   • Dip: Buy on {:.1f}% drop", -dip_threshold);
      std::println("   • MA Crossover: 5-period crosses 20-period");
      std::println("   • Mean Reversion: >2 std devs below MA");
      std::println("   • Volatility Breakout: Expansion from compression");
      std::println("   • Relative Strength: Outperform market by >0.5%");
      std::println("   • Position size: ${:.0f} per trade\n", notional_amount);
    }
    std::println("Monitoring {} stocks and {} crypto (polling every 60s, alert "
                 "threshold: {:.1f}%)",
                 stocks.size(), crypto.size(), alert_threshold);
    std::println("Press Ctrl+C to stop\n");

    while (true) {
      auto now = std::chrono::system_clock::now();
      std::println("\n⏰ Update at {:%Y-%m-%d %H:%M:%S}", now);

      // Get current positions (for display and strategy check)
      auto existing_positions = std::set<std::string>{};
      auto positions = client.get_positions();
      if (positions) {
        try {
          auto positions_json = json::parse(positions.value());

          // Build set of existing position symbols
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
                           colour, symbol, qty, avg_entry, current,
                           market_value, unrealized_plpc, strategy,
                           colour_reset);
            }
            std::println("");

            // Evaluate exit conditions with multiple strategies
            if (strategies_enabled) {
              for (const auto &pos : positions_json) {
                auto symbol = pos["symbol"].get<std::string>();
                auto unrealized_pl =
                    std::stod(pos["unrealized_pl"].get<std::string>());
                auto cost_basis =
                    std::stod(pos["cost_basis"].get<std::string>());
                auto current_price =
                    std::stod(pos["current_price"].get<std::string>());
                auto avg_entry =
                    std::stod(pos["avg_entry_price"].get<std::string>());
                auto pl_pct = (current_price - avg_entry) / avg_entry;

                // Update peak price for trailing stop
                if (not position_peaks.contains(symbol))
                  position_peaks[symbol] = current_price;
                else if (current_price > position_peaks[symbol])
                  position_peaks[symbol] = current_price;

                // Calculate trailing stop trigger
                auto peak = position_peaks[symbol];
                auto trailing_stop_price = peak * (1.0 + trailing_stop_pct);
                auto trailing_stop_triggered = current_price < trailing_stop_price;

                // Exit conditions: dollar-based OR percentage-based OR trailing stop
                auto should_exit =
                    unrealized_pl >= take_profit_threshold or   // Dollar profit
                    pl_pct >= take_profit_pct or                // Percentage profit
                    unrealized_pl <= stop_loss_amount or        // Dollar stop loss
                    pl_pct <= stop_loss_pct or                  // Percentage stop loss
                    trailing_stop_triggered;                    // Trailing stop

                if (should_exit) {
                  auto profit_percent = (unrealized_pl / cost_basis) * 100.0;
                  auto strategy = position_strategies.contains(symbol)
                                      ? position_strategies[symbol]
                                      : "manual";

                  // Determine exit reason
                  auto exit_reason = std::string{};
                  if (trailing_stop_triggered)
                    exit_reason = "TRAILING STOP";
                  else if (unrealized_pl > 0.0)
                    exit_reason = "PROFIT TARGET";
                  else
                    exit_reason = "STOP LOSS";

                  std::println(
                      "{} {}: {} ${:.2f} ({:.2f}%) from {}",
                      unrealized_pl > 0.0 ? "💰" : "🛑",
                      exit_reason, symbol, unrealized_pl, profit_percent, strategy);
                  std::println("   Closing position...");

                  auto close_result = client.close_position(symbol);
                  if (close_result) {
                    std::println("✅ Position closed: {}", symbol);
                    existing_positions.erase(symbol);

                    // Update strategy stats
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
                  } else {
                    std::println("❌ Failed to close position: {}", symbol);
                  }
                }
              }
            }
          }
        } catch (...) {
          // Silently ignore position parsing errors
        }
      }

      // Fetch stock snapshots
      auto stock_snapshots = client.get_snapshots(stocks);

      // Fetch crypto snapshots
      auto crypto_snapshots = client.get_crypto_snapshots(crypto);

      print_header();

      // Display stocks
      if (stock_snapshots) {
        for (const auto &symbol : stocks) {
          if (stock_snapshots->contains(symbol)) {
            auto &snap = stock_snapshots->at(symbol);
            auto &history = price_history[symbol];

            print_snapshot(symbol, snap, history);

            // Evaluate all strategies if enabled
            if (strategies_enabled and
                not existing_positions.contains(symbol)) {
              auto signals = std::vector<lft::StrategySignal>{
                  lft::Strategies::evaluate_dip(history, dip_threshold),
                  lft::Strategies::evaluate_ma_crossover(history),
                  lft::Strategies::evaluate_mean_reversion(history),
                  lft::Strategies::evaluate_volatility_breakout(history),
                  lft::Strategies::evaluate_relative_strength(history,
                                                              price_history)};

              // Count signals generated
              for (const auto &signal : signals) {
                if (signal.should_buy)
                  ++strategy_stats[signal.strategy_name].signals_generated;
              }

              // Execute first signal that fires (priority order)
              for (const auto &signal : signals) {
                if (signal.should_buy) {
                  std::println("{}🚨 SIGNAL: {} - {}{}", colour_cyan,
                               signal.strategy_name, signal.reason,
                               colour_reset);
                  std::println("   Buying ${:.0f} of {}...", notional_amount,
                               symbol);

                  auto order =
                      client.place_order(symbol, "buy", notional_amount);
                  if (order) {
                    try {
                      auto order_json = json::parse(order.value());
                      auto order_id = order_json["id"].get<std::string>();
                      std::println("✅ Order placed: {}", order_id);
                      existing_positions.insert(symbol);
                      position_strategies[symbol] = signal.strategy_name;
                      ++strategy_stats[signal.strategy_name].trades_executed;
                    } catch (...) {
                      std::println("✅ Order placed");
                      existing_positions.insert(symbol);
                      position_strategies[symbol] = signal.strategy_name;
                      ++strategy_stats[signal.strategy_name].trades_executed;
                    }
                  } else {
                    std::println("❌ Order failed");
                  }

                  // Only execute one strategy per symbol per interval
                  break;
                }
              }
            }
          } else {
            std::println("{:<10} No data available", symbol);
          }
        }
      } else {
        std::println("❌ Failed to fetch stock snapshots");
      }

      // Display crypto
      if (crypto_snapshots) {
        for (const auto &symbol : crypto) {
          if (crypto_snapshots->contains(symbol)) {
            auto &snap = crypto_snapshots->at(symbol);
            auto &history = price_history[symbol];

            print_snapshot(symbol, snap, history);

            // Evaluate all strategies if enabled
            if (strategies_enabled and
                not existing_positions.contains(symbol)) {
              auto signals = std::vector<lft::StrategySignal>{
                  lft::Strategies::evaluate_dip(history, dip_threshold),
                  lft::Strategies::evaluate_ma_crossover(history),
                  lft::Strategies::evaluate_mean_reversion(history),
                  lft::Strategies::evaluate_volatility_breakout(history),
                  lft::Strategies::evaluate_relative_strength(history,
                                                              price_history)};

              // Count signals generated
              for (const auto &signal : signals) {
                if (signal.should_buy)
                  ++strategy_stats[signal.strategy_name].signals_generated;
              }

              // Execute first signal that fires (priority order)
              for (const auto &signal : signals) {
                if (signal.should_buy) {
                  std::println("{}🚨 SIGNAL: {} - {}{}", colour_cyan,
                               signal.strategy_name, signal.reason,
                               colour_reset);
                  std::println("   Buying ${:.0f} of {}...", notional_amount,
                               symbol);

                  auto order =
                      client.place_order(symbol, "buy", notional_amount);
                  if (order) {
                    try {
                      auto order_json = json::parse(order.value());
                      auto order_id = order_json["id"].get<std::string>();
                      std::println("✅ Order placed: {}", order_id);
                      existing_positions.insert(symbol);
                      position_strategies[symbol] = signal.strategy_name;
                      ++strategy_stats[signal.strategy_name].trades_executed;
                    } catch (...) {
                      std::println("✅ Order placed");
                      existing_positions.insert(symbol);
                      position_strategies[symbol] = signal.strategy_name;
                      ++strategy_stats[signal.strategy_name].trades_executed;
                    }
                  } else {
                    std::println("❌ Order failed");
                  }

                  // Only execute one strategy per symbol per interval
                  break;
                }
              }
            }
          } else {
            std::println("{:<10} No data available", symbol);
          }
        }
      } else {
        std::println("❌ Failed to fetch crypto snapshots");
      }

      // Display strategy performance
      if (strategies_enabled)
        print_strategy_stats();

      std::this_thread::sleep_for(60s);
    }

  } catch (const std::exception &e) {
    std::println("Error: {}", e.what());
    return 1;
  }

  return 0;
}
