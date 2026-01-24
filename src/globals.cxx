// Global state for position tracking across phases
// These maps persist across check_entries and check_exits calls

#include <chrono>
#include <map>
#include <string>

// Track which strategy entered each position
std::map<std::string, std::string> position_strategies;

// Track peak prices for trailing stop loss
std::map<std::string, double> position_peaks;

// Track when each position was entered
std::map<std::string, std::chrono::system_clock::time_point> position_entry_times;
