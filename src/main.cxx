#include "lft.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <print>
#include <set>
#include <thread>

// LFT - Low Frequency Trader

int main() {
  std::println("🚀 LFT - Low Frequency Trader V2");
  using namespace std::chrono_literals;

  // Create connection to exchange
  auto client = lft::AlpacaClient{};

  // Define session duration
  const auto session_start = std::chrono::system_clock::now();
  const auto session_end = lft::next_whole_hour(session_start);
  const auto eod = lft::eod_cutoff_time(session_start); // 3:55 PM ET today

  // Fetch 30 days of 15-minute bars for calibration
  std::println("📊 Fetching historical data...");
  const auto bars = lft::fetch_bars(client);

  // Calibrate strategies using historic data with fixed starting capital
  constexpr auto backtest_capital = 100000.0;
  std::println("🎯 Calibrating strategies with ${:.2f} starting capital...",
               backtest_capital);
  const auto enabled_strategies = lft::calibrate(bars, backtest_capital);

  //   std::println("🔄 Starting event loop until {:%H:%M:%S}\n", session_end);

  // Run 60 minute cycle, synchronised to whole hour
  // Create intervals
  auto next_entry = lft::next_15_minute_bar(session_start);
  auto next_exit = lft::next_minute_at_35_seconds(session_start);
  auto liquidated = false;

  for (auto now = std::chrono::system_clock::now(); now < session_end;
       now = std::chrono::system_clock::now()) {

    const auto remaining =
        std::chrono::duration_cast<std::chrono::minutes>(session_end - now);
    std::println(
        "\n{:%H:%M:%S} | Session ends: {:%H:%M:%S} | Remaining: {} min",
        std::chrono::floor<std::chrono::seconds>(now),
        std::chrono::floor<std::chrono::seconds>(session_end),
        remaining.count());

    // Display balances and positions
    lft::display_account_summary(client);

    // Check market hours
    const auto is_closed = not lft::is_market_hours(now);
    std::println("\n📊 Market: {}", is_closed ? "CLOSED" : "OPEN");

    if (is_closed or liquidated) {
      std::this_thread::sleep_for(1min);
      continue;
    }

    // if (not liquidated) {

    // Show time until market close
    const auto time_until_close = eod - now;
    const auto hours =
        std::chrono::duration_cast<std::chrono::hours>(time_until_close);
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(
        time_until_close - hours);
    std::println("📈 Market open - closes in {}h {}min", hours.count(),
                 minutes.count());

    // Liquidate all positions at the end of trading
    if (now >= eod) {
      std::println("🚨 EOD cutoff - liquidating all positions");
      lft::liquidate_all(client);
      liquidated = true;
      continue;
    }

    // Get current positions for evaluation
    auto positions = client.get_positions();
    auto symbols_in_use = std::set<std::string>{};
    for (const auto &pos : positions)
      symbols_in_use.insert(pos.symbol);

    // Evaluate market every minute (shows prices, spreads, and strategy
    // signals)
    auto evaluation =
        lft::evaluate_market(client, enabled_strategies, symbols_in_use);
    lft::display_evaluation(evaluation, enabled_strategies, now);

    // Check exits every minute at :35 (after bar recalculation)
    if (now >= next_exit) {
      lft::check_exits(client, now);
      next_exit = lft::next_minute_at_35_seconds(now);
    }

    // Execute entry trades every 15 minutes (aligned to :00, :15, :30, :45)
    if (now >= next_entry) {
      std::println("\n💼 Executing entry trades at {:%H:%M:%S}",
                   std::chrono::floor<std::chrono::seconds>(now));
      lft::check_entries(client, enabled_strategies);
      next_entry = lft::next_15_minute_bar(now);
    }
    // }

    // Sleep for 1 minute before next cycle
    // std::this_thread::sleep_for(1min);
  }

  std::println("\n✅ Session complete - exiting for restart");
  return 0;
}