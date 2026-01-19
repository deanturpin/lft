#include "lft.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <print>
#include <sstream>
#include <thread>

// LFT - Low Frequency Trader

namespace {
// Parse ISO 8601 timestamp and convert to local time string
auto to_local_time(std::string_view iso_timestamp) -> std::string {
  // Parse ISO 8601: 2026-01-20T09:30:00-05:00
  std::tm tm = {};
  auto offset_hours = 0;
  auto offset_mins = 0;

  std::istringstream ss{std::string{iso_timestamp}};
  ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

  if (ss.fail())
    return std::string{iso_timestamp}; // Return original if parse fails

  // Parse timezone offset
  auto offset_sign = '+';
  ss >> offset_sign >> offset_hours;
  if (ss.peek() == ':')
    ss.ignore();
  ss >> offset_mins;

  // Convert to UTC by subtracting offset
  auto utc_time = std::mktime(&tm);
  if (offset_sign == '-')
    utc_time += (offset_hours * 3600 + offset_mins * 60);
  else
    utc_time -= (offset_hours * 3600 + offset_mins * 60);

  // Convert to local time
  auto local_tm = *std::localtime(&utc_time);

  // Format as readable string
  std::ostringstream out{};
  out << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S %Z");
  return out.str();
}
} // namespace

int main() {
  std::println("🚀 LFT - Low Frequency Trader V2");
  using namespace std::chrono_literals;

  // Create connection to exchange
  auto client = lft::AlpacaClient{};

  // Define session duration
  const auto session_start = std::chrono::system_clock::now();
  const auto session_end = lft::next_whole_hour(session_start);
  const auto eod = lft::eod_cutoff_time(session_start); // 3:55 PM ET today

  // Get account information first to get starting capital
  auto starting_capital = 100000.0; // Default fallback
  if (auto account_result = client.get_account()) {
    auto account_json = nlohmann::json::parse(*account_result);
    const auto equity =
        account_json.contains("equity") and not account_json["equity"].is_null()
            ? std::stod(account_json["equity"].get<std::string>())
            : 0.0;
    const auto buying_power =
        account_json.contains("buying_power") and
                not account_json["buying_power"].is_null()
            ? std::stod(account_json["buying_power"].get<std::string>())
            : 0.0;
    const auto daytrading_buying_power =
        account_json.contains("daytrading_buying_power") and
                not account_json["daytrading_buying_power"].is_null()
            ? std::stod(
                  account_json["daytrading_buying_power"].get<std::string>())
            : 0.0;

    // Use actual equity as starting capital
    if (equity > 0.0)
      starting_capital = equity;

    std::println("\n💰 Account Status:");
    std::println("  Current Equity:    ${:>12.2f}", equity);
    std::println("  Buying Power:      ${:>12.2f}", buying_power);
    std::println("  Day Trade BP:      ${:>12.2f}", daytrading_buying_power);

    // Get and display market clock information
    if (auto clock_result = client.get_market_clock()) {
      const auto &clock = *clock_result;
      std::println("\n📅 Market Status:");
      std::println("  Market:            {}",
                   clock.is_open ? "OPEN" : "CLOSED");
      std::println("  Next Open:         {}", to_local_time(clock.next_open));
      std::println("  Next Close:        {}", to_local_time(clock.next_close));
    }
    std::println("");
  }

  // Fetch 30 days of 15-minute bars for calibration
  std::println("📊 Fetching historical data...");
  const auto bars = lft::fetch_bars(client);

  // Calibrate strategies using historic data and actual account equity
  std::println("🎯 Calibrating strategies...");
  const auto enabled_strategies = lft::calibrate(bars, starting_capital);

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

    // Market summary: Display current conditions (shows even when market closed)
    lft::display_market_summary(client);

    // Check market hours using API (primary) with local calculation fallback
    auto clock_result = client.get_market_clock();
    auto is_open = false;

    if (clock_result) {
      is_open = clock_result->is_open;
      std::println("\n📊 Market: {}", is_open ? "OPEN" : "CLOSED");
      std::println("📅 Next open:  {}", to_local_time(clock_result->next_open));
      std::println("📅 Next close: {}",
                   to_local_time(clock_result->next_close));

      if (not is_open) {
        std::this_thread::sleep_for(1min);
        continue;
      }
    } else {
      // API check failed - fallback to local calculation
      std::println("\n⚠️  Clock API failed, using local time calculation");
      is_open = lft::is_market_hours(now);

      if (not is_open) {
        std::println("⏰ Market closed (fallback check)");
        std::this_thread::sleep_for(1min);
        continue;
      }
    }

    if (not liquidated) {

      // Show time until market close
      const auto time_until_close =
          std::chrono::duration_cast<std::chrono::minutes>(eod - now);
      std::println("📈 Market open - closes in {}min",
                   time_until_close.count());

      // Liquidate all positions at the end of trading
      if (now >= eod) {
        std::println("🚨 EOD cutoff - liquidating all positions");
        lft::liquidate_all(client);
        liquidated = true;
        continue;
      }

      // Check exits every minute at :35 (after bar recalculation)
      if (now >= next_exit) {
        std::println("📤 Checking exits at {:%H:%M:%S}",
                     std::chrono::floor<std::chrono::seconds>(now));
        lft::check_exits(client);
        next_exit = lft::next_minute_at_35_seconds(now);
      }

      // Check entries every 15 minutes (aligned to :00, :15, :30, :45)
      if (now >= next_entry) {
        std::println("📥 Evaluating entries at {:%H:%M:%S}",
                     std::chrono::floor<std::chrono::seconds>(now));
        lft::evaluate_entries(client, enabled_strategies);
        next_entry = lft::next_15_minute_bar(now);
      }
    }

    // Sleep for 1 minute before next cycle
    std::this_thread::sleep_for(1min);
  }

  std::println("\n✅ Session complete - exiting for restart");
  return 0;
}