#pragma once

#include "alpaca_client.h"
#include "bps_utils.h"
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace lft {

// Strategy result indicating whether to buy and why
struct StrategySignal {
    bool should_buy{false};
    std::string reason;
    std::string strategy_name;
    double confidence{1.0};      // Signal confidence: 0.0-1.0 (reduced by noise/low volume)
    double expected_move_bps{0.0}; // Expected price move in basis points (for cost/edge calculation)
};

// Strategy configuration (exit criteria are now unified, not per-strategy)
struct StrategyConfig {
    std::string name;
    bool enabled{false};
    int trades_closed{};        // From calibration
    double net_profit{};        // From calibration
    double win_rate{};          // From calibration
    double expected_move_bps{}; // Average forward return after signal (from calibration)
};

// Performance tracking for each strategy
struct StrategyStats {
    std::string name;
    int signals_generated{};
    int trades_executed{};
    int trades_closed{};
    int profitable_trades{};
    int losing_trades{};
    double total_profit{};
    double total_loss{};

    // Forward return tracking (for expected move calculation)
    double total_forward_returns_bps{}; // Sum of forward returns from all signals
    int forward_return_samples{};       // Number of signals measured
    double total_win_bps{};             // Sum of winning trade sizes
    double total_loss_bps{};            // Sum of losing trade sizes (negative)

    // Trade duration tracking
    std::size_t total_duration_bars{};  // Sum of all trade durations in bars
    std::size_t max_duration_bars{};    // Longest trade duration
    std::size_t min_duration_bars{std::numeric_limits<std::size_t>::max()}; // Shortest trade

    double win_rate() const {
        return trades_closed > 0 ? (static_cast<double>(profitable_trades) / trades_closed) * 100.0 : 0.0;
    }

    double net_profit() const {
        return total_profit + total_loss;
    }

    double avg_forward_return_bps() const {
        return forward_return_samples > 0 ? total_forward_returns_bps / forward_return_samples : 0.0;
    }

    double avg_win_bps() const {
        return profitable_trades > 0 ? total_win_bps / profitable_trades : 0.0;
    }

    double avg_loss_bps() const {
        return losing_trades > 0 ? total_loss_bps / losing_trades : 0.0;
    }

    double avg_duration_bars() const {
        return trades_closed > 0 ? static_cast<double>(total_duration_bars) / trades_closed : 0.0;
    }

    std::size_t median_duration_bars() const {
        return trades_closed > 0 ? total_duration_bars / trades_closed : 0;
    }
};

// Price history with multiple timeframes
struct PriceHistory {
    std::vector<double> prices;
    std::vector<double> highs;     // High prices for noise calculation
    std::vector<double> lows;      // Low prices for noise calculation
    std::vector<long> volumes;     // Trading volumes
    double last_price{};
    double change_percent{};
    bool has_history{false};
    std::string last_trade_timestamp;  // Track last trade to avoid duplicates

    // Member function declarations (implementations in strategies.cxx)
    void add_price_with_timestamp(double, std::string_view);
    void add_price(double);
    void add_bar(double, double, double, long);
    double moving_average(size_t) const;
    double volatility() const;
    double recent_noise(size_t = 20) const;
    long avg_volume() const;
    double volume_factor() const;
};

// Strategy evaluation functions
class Strategies {
public:
    // Buy on price dip
    static StrategySignal evaluate_dip(const PriceHistory&, double);

    // Buy on moving average crossover
    static StrategySignal evaluate_ma_crossover(const PriceHistory&);

    // Buy on mean reversion
    static StrategySignal evaluate_mean_reversion(const PriceHistory&);

    // Buy on volatility breakout
    static StrategySignal evaluate_volatility_breakout(const PriceHistory&);

    // Buy on relative strength (compare to market average)
    static StrategySignal evaluate_relative_strength(const PriceHistory&, const std::map<std::string, PriceHistory>&);

    // Buy on volume surge with momentum
    static StrategySignal evaluate_volume_surge(const PriceHistory&);

    // Trade eligibility helpers (used by can_enter_position in lft.cxx)
    static double calculate_spread_bps(const Snapshot&);
    static double calculate_volume_ratio(const PriceHistory&);
};

} // namespace lft
