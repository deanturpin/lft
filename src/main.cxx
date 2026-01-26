#include "lft.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <print>
#include <set>
#include <thread>

// RISK OFF INVARIANT
// risk_off is a latch:
// - starts false at session open
// - any predicate may set it true
// - once true, it remains true until next session reset/*

/*
// risk_off / riskOff (very common)

// panic_exit / panicExit (clear and blunt)

// kill_switch / killSwitch (common in trading systems)

// force_flat / forceFlat (describes exactly what happens)

// hard_exit / hardExit (contrasts with normal exits)


// Normal exits

// Usually called:

// strategy_exit / strategyExit

// managed_exit / managedExit

// soft_exit / softExit (only if you contrast with hard exits)

// or simply exits with reasons TP, SL, TSL

// I’d go with strategy_exit for clarity.


enum class ExitReason {
  TakeProfit,
  StopLoss,
  TrailingStop,
  EodForceFlat,
  KillSwitch,        // e.g. big loss / crash rule
};

tp = 3 sl = 2 tsl = 1 kill switch sl = 3.5

    TP = +3 %

         SL = −2 %

              TSL =
             1 % (probably with delayed activation, but leaving that aside)

                     EOD = 16 : 00 ET

                 last entry = EOD − 30m

    force flat = EOD − 3m

    kill switch stop = (bigger than −2 %, e.g. −3.5 %);
if you
  keep it at −2 % it’s not a kill switch,
      it’s just SL

          eod = 4pm no_more_trades = eod - 30 liquidate =
                                         eod - 3

                                         // entries (15 bar)
                                         if (now < no_more_trades) {
    // evaluate
  }

// normal exits (15 bar)
if (tp / sl / tsl)

  // emergency exits (1 bar)
  if (now > liquidate or circuit break sl)

    tp = 3;
sl = 2;
tsl = 1;

eod = 16 : 00;
last_entry_time = eod - 30min;
force_flat_time = eod - 3min;

// entries
if (now < last_entry_time) {
  // evaluate entries on 15m bar
}

// strategy exits (managed exits)
if (tp_hit)
  exit(ExitReason::TakeProfit);
if (sl_hit)
  exit(ExitReason::StopLoss);
if (tsl_hit)
  exit(ExitReason::TrailingStop);

// hard exits (risk-off / force-flat)
if (now >= force_flat_time)
  exit_all(ExitReason::EodForceFlat);
if (kill_switch_hit)
  exit(ExitReason::KillSwitch);
*/

// LFT - Low Frequency Trader

int main() {
  std::println("🚀 LFT - Low Frequency Trader V2");
  using namespace std::chrono_literals;

  // Create connection to exchange
  auto client = AlpacaClient{};

  // Define session duration
  const auto session_start = std::chrono::system_clock::now();
  const auto session_end = next_whole_hour(session_start);
  const auto eod = eod_cutoff_time(session_start); // 3:50 PM ET today
  const auto risk_off_ends = risk_on_time(session_start); // 10:00 AM ET today

  // Fetch 30 days of 15-minute bars for calibration
  std::println("📊 Fetching historical data...");
  const auto bars = fetch_bars(client);

  // Calibrate strategies using historic data with fixed starting capital
  constexpr auto backtest_capital = 100000.0;
  std::println("🎯 Calibrating strategies with ${:.2f} starting capital...",
               backtest_capital);
  const auto enabled_strategies = calibrate(bars, backtest_capital);

  //   std::println("🔄 Starting event loop until {:%H:%M:%S}\n", session_end);

  // Run 60 minute cycle, synchronised to whole hour
  // Create intervals
  auto next_entry = next_15_minute_bar(session_start);
  auto next_exit = next_minute_at_35_seconds(session_start);
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

    // Display next scheduled event times
    std::println("\n⏰ Next Events:");
    std::println("  Strategy Cycle:  {:%H:%M:%S}  (entries + TP/SL/trailing)",
                 std::chrono::floor<std::chrono::seconds>(next_entry));
    std::println("  Panic Check:     {:%H:%M:%S}  (panic stops + EOD liquidation)",
                 std::chrono::floor<std::chrono::seconds>(next_exit));

    // Display balances and positions
    display_account_summary(client);

    // Check market hours
    const auto is_closed = not is_market_hours(now);
    std::println("\n📊 Market: {}", is_closed ? "CLOSED" : "OPEN");

    if (is_closed or liquidated) {
      std::this_thread::sleep_for(1min);
      continue;
    }

    // Show time until EOD cutoff
    const auto time_until_close = eod - now;
    const auto hours =
        std::chrono::duration_cast<std::chrono::hours>(time_until_close);
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(
        time_until_close - hours);
    std::println("📈 Market open - EOD cutoff in {}h {}min", hours.count(),
                 minutes.count());

    // Get current positions for evaluation
    auto positions = client.get_positions();
    auto symbols_in_use = std::set<std::string>{};
    for (const auto &pos : positions)
      symbols_in_use.insert(pos.symbol);

    // Evaluate market every minute (shows prices, spreads, and strategy
    // signals)
    auto evaluation =
        evaluate_market(client, enabled_strategies, symbols_in_use);
    display_evaluation(evaluation, enabled_strategies, now);

    // Check panic exits every minute at :35 (fast reaction to all emergency conditions)
    if (now >= next_exit) {
      check_panic_exits(client, now, eod);
      next_exit = next_minute_at_35_seconds(now);
    }

    // Execute entry trades every 15 minutes (aligned to :00, :15, :30, :45)
    // Risk-off before 10:00 AM ET (opening volatility period)
    // Also check normal exits (TP/SL/trailing) at same frequency as entries
    if (now >= next_entry) {
      const auto risk_off = now < risk_off_ends;
      if (not risk_off) {
        std::println("\n💼 Executing entry trades at {:%H:%M:%S}",
                     std::chrono::floor<std::chrono::seconds>(now));
        check_entries(client, enabled_strategies);
      } else {
        std::println("\n⚠️  Risk-off: No entries until {:%H:%M:%S}",
                     std::chrono::floor<std::chrono::seconds>(risk_off_ends));
      }
      check_normal_exits(client, now);
      next_entry = next_15_minute_bar(now);
    }
    // }

    // Sleep for 1 minute before next cycle
    // std::this_thread::sleep_for(1min);
  }

  std::println("\n✅ Session complete - exiting for restart");
  return 0;
}