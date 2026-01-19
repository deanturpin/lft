#pragma once

#include "alpaca_client.h"
#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace lft {

// Market assessment result
struct MarketAssessment {
  std::string summary;
  bool tradeable;
};

// Backtesting position tracking
struct BacktestPosition {
  std::string symbol;
  std::string strategy;
  double entry_price{};
  double quantity{};
  std::size_t entry_bar_index{};
  double peak_price{};
};

// Forward declare StrategyStats (defined in strategies.h)
struct StrategyStats;

// Data fetching and assessment
std::vector<Snapshot> fetch_snapshots(AlpacaClient &);
std::map<std::string, std::vector<Bar>> fetch_bars(AlpacaClient &);
MarketAssessment assess_market_conditions(AlpacaClient &, const std::vector<Snapshot> &);

// Phase 1: Calibrate strategies on historic bar data
// Returns map of strategy name -> enabled status
std::map<std::string, bool> calibrate(const std::map<std::string, std::vector<Bar>> &, double);

// Phase 2: Evaluate entry signals for all symbols (every 15 minutes)
void evaluate_entries(AlpacaClient &, const std::map<std::string, bool> &);

// Phase 3: Check exit conditions for all positions (every minute)
void check_exits(AlpacaClient &);

// Phase 4: Emergency liquidation of all equity positions (EOD)
void liquidate_all(AlpacaClient &);

// Timing helpers
std::chrono::system_clock::time_point next_whole_hour(std::chrono::system_clock::time_point);
std::chrono::system_clock::time_point next_15_minute_bar(std::chrono::system_clock::time_point);
std::chrono::system_clock::time_point next_minute_at_35_seconds(std::chrono::system_clock::time_point);
std::chrono::system_clock::time_point eod_cutoff_time(std::chrono::system_clock::time_point);
bool is_market_hours(std::chrono::system_clock::time_point);

} // namespace lft
