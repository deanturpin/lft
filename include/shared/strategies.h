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
    double last_price{};
    double change_percent{};
    bool has_history{false};

    void add_price(double price) {
        prices.push_back(price);
        // Keep last 100 data points for moving averages
        if (prices.size() > 100)
            prices.erase(prices.begin());

        if (prices.size() >= 2) {
            last_price = prices[prices.size() - 2];
            change_percent = ((price - last_price) / last_price) * 100.0;
            has_history = true;
        }
    }

    double moving_average(size_t periods) const {
        if (prices.size() < periods)
            return 0.0;

        auto sum = 0.0;
        for (auto i = prices.size() - periods; i < prices.size(); ++i)
            sum += prices[i];

        return sum / periods;
    }

    double volatility() const {
        if (prices.size() < 2)
            return 0.0;

        auto mean = moving_average(prices.size());
        auto variance = 0.0;

        for (const auto& price : prices) {
            auto diff = price - mean;
            variance += diff * diff;
        }

        return std::sqrt(variance / prices.size());
    }
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
};

} // namespace lft
