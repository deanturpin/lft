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

// Strategy-specific exit configuration
struct StrategyConfig {
    std::string name;
    bool enabled{false};
    double take_profit_pct{};
    double stop_loss_pct{};
    double trailing_stop_pct{};
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

    // For live trading with timestamps - only add if trade is new
    void add_price_with_timestamp(double price, std::string_view timestamp) {
        // Only add if this is a NEW trade (different timestamp)
        if (timestamp.empty() or timestamp != last_trade_timestamp) {
            prices.push_back(price);
            last_trade_timestamp = std::string{timestamp};

            // Keep last 100 data points for moving averages
            if (prices.size() > 100)
                prices.erase(prices.begin());

            if (prices.size() >= 2) {
                last_price = prices[prices.size() - 2];
                change_percent = ((price - last_price) / last_price) * 100.0;
                has_history = true;
            }
        }
        // If same timestamp: do nothing, preserve existing change_percent
    }

    // For backtesting without timestamps - always add
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

    void add_bar(double close, double high, double low, long volume) {
        add_price(close);
        highs.push_back(high);
        lows.push_back(low);
        volumes.push_back(volume);

        // Keep synced with prices
        if (highs.size() > 100)
            highs.erase(highs.begin());
        if (lows.size() > 100)
            lows.erase(lows.begin());
        if (volumes.size() > 100)
            volumes.erase(volumes.begin());
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

    // Calculate average noise over recent periods (high-low range as % of close)
    double recent_noise(size_t periods = 20) const {
        if (highs.size() < periods or lows.size() < periods or prices.size() < periods)
            return 0.0;

        auto total_noise = 0.0;
        auto start_idx = prices.size() - periods;

        for (auto i = start_idx; i < prices.size(); ++i) {
            auto noise = (highs[i] - lows[i]) / prices[i];
            total_noise += noise;
        }

        return total_noise / periods;
    }

    // Calculate average volume
    long avg_volume() const {
        if (volumes.empty())
            return 0;

        auto sum = 0L;
        for (auto vol : volumes)
            sum += vol;

        return sum / static_cast<long>(volumes.size());
    }

    // Volume factor for signal confidence (1.0 = normal, >1.0 = low volume penalty)
    double volume_factor() const {
        if (volumes.empty())
            return 1.0;

        auto current_vol = volumes.back();
        auto avg = avg_volume();

        if (avg == 0)
            return 1.0;

        // Low volume (<50% avg) → penalise confidence
        auto vol_ratio = static_cast<double>(current_vol) / avg;
        if (vol_ratio < 0.5)
            return 1.5;  // 50% confidence penalty
        else if (vol_ratio < 0.75)
            return 1.2;  // 20% confidence penalty
        else
            return 1.0;  // Normal confidence
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

    // Buy on volume surge with momentum
    static StrategySignal evaluate_volume_surge(const PriceHistory&);
};

} // namespace lft
