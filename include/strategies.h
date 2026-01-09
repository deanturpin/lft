#pragma once

#include "alpaca_client.h"
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
    double confidence{1.0};  // Signal confidence: 0.0-1.0 (reduced by noise/low volume)
};

// Strategy configuration (exit criteria are now unified, not per-strategy)
struct StrategyConfig {
    std::string name;
    bool enabled{false};
    int trades_closed{};  // From calibration
    double net_profit{};  // From calibration
    double win_rate{};    // From calibration
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

    double win_rate() const {
        return trades_closed > 0 ? (static_cast<double>(profitable_trades) / trades_closed) * 100.0 : 0.0;
    }

    double net_profit() const {
        return total_profit + total_loss;
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
};

} // namespace lft
