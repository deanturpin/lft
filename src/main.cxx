#include "lft.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <print>
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

  // Calibrate strategies using historic data
  std::println("🎯 Calibrating strategies...");
  const auto enabled_strategies = lft::calibrate(bars);

  // Display account information
  if (auto account_result = client.get_account()) {
    auto account_json = nlohmann::json::parse(*account_result);
    const auto equity = account_json.contains("equity") and not account_json["equity"].is_null()
                            ? std::stod(account_json["equity"].get<std::string>())
                            : 0.0;
    const auto buying_power =
        account_json.contains("buying_power") and not account_json["buying_power"].is_null()
            ? std::stod(account_json["buying_power"].get<std::string>())
            : 0.0;
    const auto daytrading_buying_power =
        account_json.contains("daytrading_buying_power") and
                not account_json["daytrading_buying_power"].is_null()
            ? std::stod(account_json["daytrading_buying_power"].get<std::string>())
            : 0.0;

    std::println("\n💰 Account Status:");
    std::println("  Current Equity:    ${:>12.2f}", equity);
    std::println("  Buying Power:      ${:>12.2f}", buying_power);
    std::println("  Day Trade BP:      ${:>12.2f}", daytrading_buying_power);
    std::println("");
  }

  //   std::println("🔄 Starting event loop until {:%H:%M:%S}\n", session_end);

  // Run 60 minute cycle, synchronised to whole hour
  // Create intervals
  auto next_entry = lft::next_15_minute_bar(session_start);
  auto next_exit = lft::next_minute_at_35_seconds(session_start);
  auto liquidated = false;
  auto now = std::chrono::system_clock::now();

  do {
    // Update current time each iteration
    now = std::chrono::system_clock::now();
    const auto remaining = std::chrono::duration_cast<std::chrono::minutes>(session_end - now);
    std::println("\n🔄 Loop: {:%H:%M:%S} | Session ends: {:%H:%M:%S} | Remaining: {}min",
                 now, session_end, remaining.count());

    // Prices fetched on a 1 minute cycle
    std::this_thread::sleep_for(1min);

    // Fetch latest snapshots for all watchlist symbols
    auto snapshots = lft::fetch_snapshots(client);

    // Assess market conditions (spread, volume, volatility)
    auto assessment = lft::assess_market_conditions(snapshots);
    std::println("{}", assessment.summary);

    // Only trade in normal market hours
    if (not lft::is_market_hours(now)) {
      const auto now_t = std::chrono::system_clock::to_time_t(now);
      auto now_tm = *std::localtime(&now_t);

      // Check if weekend
      if (now_tm.tm_wday == 0 or now_tm.tm_wday == 6) {
        const auto day_name = now_tm.tm_wday == 0 ? "Sunday" : "Saturday";
        std::println("⏰ Market closed - {} (reopens Monday 9:30 AM)", day_name);
      } else {
        // Weekday but outside hours - calculate time until next open
        auto next_open = now_tm;
        next_open.tm_hour = 9;
        next_open.tm_min = 30;
        next_open.tm_sec = 0;

        // If we're past 9:30 today, market open is tomorrow
        if (now_tm.tm_hour > 9 or (now_tm.tm_hour == 9 and now_tm.tm_min >= 30))
          next_open.tm_mday += 1;

        const auto next_open_time = std::chrono::system_clock::from_time_t(std::mktime(&next_open));
        const auto wait_duration = std::chrono::duration_cast<std::chrono::hours>(next_open_time - now);

        std::println("⏰ Market closed - opens in {}h", wait_duration.count());
      }
      continue;
    }

    if (liquidated)
      continue;

    // Show time until market close
    const auto time_until_close = std::chrono::duration_cast<std::chrono::minutes>(eod - now);
    std::println("📈 Market open - closes in {}min", time_until_close.count());

    // Liquidate all positions at the end of trading
    if (now >= eod) {
      std::println("🚨 EOD cutoff - liquidating all positions");
      lft::liquidate_all(client);
      liquidated = true;
      continue;
    }

    // Check exits every minute at :35 (after bar recalculation)
    if (now >= next_exit) {
      std::println("📤 Checking exits at {:%H:%M:%S}", now);
      lft::check_exits(client, now);
      next_exit = lft::next_minute_at_35_seconds(now);
    }

    // Check entries every 15 minutes (aligned to :00, :15, :30, :45)
    if (now >= next_entry) {
      std::println("📥 Evaluating entries at {:%H:%M:%S}", now);
      lft::evaluate_entries(client, enabled_strategies, now);
      next_entry = lft::next_15_minute_bar(now);
    }
  } while (now < session_end);

  std::println("\n✅ Session complete - exiting for restart");
  return 0;
}