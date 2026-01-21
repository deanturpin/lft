#pragma once

#include "alpaca_client.h"
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

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

// Market evaluation structures
struct SymbolEvaluation {
  std::string symbol;
  double price{};
  double spread_bps{};
  double edge_bps{};
  double volume_ratio{};  // Current volume as ratio of 20-bar average
  bool tradeable{};
  bool ready_to_trade{};  // True if tradeable AND has at least one signal
  std::map<std::string, bool> strategy_signals;
  std::string status_summary;
};

struct MarketEvaluation {
  std::vector<SymbolEvaluation> symbols;
  std::size_t tradeable_count{};
  double avg_spread_bps{};
  std::size_t total_signals{};
};

// Evaluate market conditions and strategy signals (runs every minute)
MarketEvaluation evaluate_market(AlpacaClient &, const std::map<std::string, bool> &, const std::set<std::string> &);
void display_evaluation(const MarketEvaluation &, const std::map<std::string, bool> &, std::chrono::system_clock::time_point);

// Phase 2: Check entry signals and execute trades (every 15 minutes)
void check_entries(AlpacaClient &, const std::map<std::string, bool> &);

// Phase 3: Check exit conditions for all positions (every minute)
void check_exits(AlpacaClient &, std::chrono::system_clock::time_point);

// Phase 4: Emergency liquidation of all equity positions (EOD)
void liquidate_all(AlpacaClient &);

// Account summary: Display account balances and positions
void display_account_summary(AlpacaClient &);

// Timing helpers
std::chrono::system_clock::time_point next_whole_hour(std::chrono::system_clock::time_point);
std::chrono::system_clock::time_point next_15_minute_bar(std::chrono::system_clock::time_point);
std::chrono::system_clock::time_point next_minute_at_35_seconds(std::chrono::system_clock::time_point);
std::chrono::system_clock::time_point eod_cutoff_time(std::chrono::system_clock::time_point);
bool is_market_hours(std::chrono::system_clock::time_point);
