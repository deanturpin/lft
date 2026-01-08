#include "strategies.h"
#include <cassert>
#include <cmath>
#include <format>

namespace lft {

StrategySignal Strategies::evaluate_dip(const PriceHistory& history, double threshold) {
    // Defensive assertions: threshold should be negative for a "dip"
    assert(threshold < 0.0 && "Dip threshold must be negative");
    assert(std::isfinite(threshold) && "Threshold must be finite");

    auto signal = StrategySignal{};
    signal.strategy_name = "dip";

    if (not history.has_history)
        return signal;

    // Validate price change is finite
    assert(std::isfinite(history.change_percent) && "Change percent must be finite");

    if (history.change_percent <= threshold) {
        signal.should_buy = true;
        signal.reason = std::format("Price dropped {:.2f}%", history.change_percent);
    }

    return signal;
}

StrategySignal Strategies::evaluate_ma_crossover(const PriceHistory& history) {
    auto signal = StrategySignal{};
    signal.strategy_name = "ma_crossover";

    // Need at least 20 data points for short MA
    if (history.prices.size() < 20)
        return signal;

    auto ma_short = history.moving_average(5);   // 5-period MA
    auto ma_long = history.moving_average(20);   // 20-period MA

    // Defensive assertions: MAs should be valid
    assert(std::isfinite(ma_short) && ma_short > 0.0 && "Short MA must be positive and finite");
    assert(std::isfinite(ma_long) && ma_long > 0.0 && "Long MA must be positive and finite");

    // Previous values to detect crossover
    if (history.prices.size() < 21)
        return signal;

    auto prev_prices = history.prices;
    prev_prices.pop_back();

    auto prev_ma_short = 0.0;
    auto prev_ma_long = 0.0;

    for (auto i = prev_prices.size() - 5; i < prev_prices.size(); ++i)
        prev_ma_short += prev_prices[i];
    prev_ma_short /= 5;

    for (auto i = prev_prices.size() - 20; i < prev_prices.size(); ++i)
        prev_ma_long += prev_prices[i];
    prev_ma_long /= 20;

    // Bullish crossover: short MA crosses above long MA
    if (prev_ma_short <= prev_ma_long and ma_short > ma_long) {
        signal.should_buy = true;
        signal.reason = std::format("MA crossover: {:.2f} > {:.2f}", ma_short, ma_long);
    }

    return signal;
}

StrategySignal Strategies::evaluate_mean_reversion(const PriceHistory& history) {
    auto signal = StrategySignal{};
    signal.strategy_name = "mean_reversion";

    if (history.prices.size() < 20)
        return signal;

    auto current_price = history.prices.back();
    auto ma = history.moving_average(20);
    auto std_dev = history.volatility();

    // Defensive assertions: validate statistical values
    assert(std::isfinite(current_price) && current_price > 0.0 && "Current price must be positive and finite");
    assert(std::isfinite(ma) && ma > 0.0 && "MA must be positive and finite");
    assert(std::isfinite(std_dev) && std_dev >= 0.0 && "Std dev must be non-negative and finite");

    // If volatility is too low (near zero), mean reversion strategy doesn't apply
    if (std_dev < 0.0001)
        return signal;

    // Buy when price is more than 2 standard deviations below MA
    auto deviation = (current_price - ma) / std_dev;

    if (deviation < -2.0) {
        signal.should_buy = true;
        signal.reason = std::format("Mean reversion: {:.2f} std devs below MA", deviation);
    }

    return signal;
}

StrategySignal Strategies::evaluate_volatility_breakout(const PriceHistory& history) {
    auto signal = StrategySignal{};
    signal.strategy_name = "volatility_breakout";

    if (history.prices.size() < 20)
        return signal;

    // Calculate recent volatility vs historical
    auto recent_volatility = 0.0;
    for (auto i = history.prices.size() - 5; i < history.prices.size() - 1; ++i) {
        auto change = std::abs((history.prices[i + 1] - history.prices[i]) / history.prices[i]);
        assert(std::isfinite(change) && "Price change must be finite");
        recent_volatility += change;
    }
    recent_volatility /= 4;

    auto historical_volatility = history.volatility();

    // Defensive assertions: validate volatility calculations
    assert(std::isfinite(recent_volatility) && recent_volatility >= 0.0 && "Recent volatility must be non-negative and finite");
    assert(std::isfinite(historical_volatility) && historical_volatility >= 0.0 && "Historical volatility must be non-negative and finite");

    // Buy when volatility expands (breakout from compression)
    if (historical_volatility > 0 and recent_volatility > historical_volatility * 1.5 and history.change_percent > 0) {
        signal.should_buy = true;
        signal.reason = std::format("Volatility breakout: {:.4f} vs {:.4f}", recent_volatility, historical_volatility);
    }

    return signal;
}

StrategySignal Strategies::evaluate_relative_strength(
    const PriceHistory& history,
    const std::map<std::string, PriceHistory>& all_histories) {

    auto signal = StrategySignal{};
    signal.strategy_name = "relative_strength";

    if (not history.has_history)
        return signal;

    // Defensive assertion: ensure we have assets to compare against
    assert(!all_histories.empty() && "Need at least one asset for relative strength");

    // Calculate average change across all assets
    auto total_change = 0.0;
    auto count = 0uz;

    for (const auto& [symbol, hist] : all_histories) {
        if (hist.has_history) {
            assert(std::isfinite(hist.change_percent) && "Change percent must be finite");
            total_change += hist.change_percent;
            ++count;
        }
    }

    if (count == 0)
        return signal;

    auto market_average = total_change / count;
    assert(std::isfinite(market_average) && "Market average must be finite");

    // Buy if this asset is outperforming market by >0.5%
    if (history.change_percent > market_average + 0.5) {
        signal.should_buy = true;
        signal.reason = std::format("Relative strength: {:.2f}% vs market {:.2f}%",
                                    history.change_percent, market_average);
    }

    return signal;
}

StrategySignal Strategies::evaluate_volume_surge(const PriceHistory& history) {
    auto signal = StrategySignal{};
    signal.strategy_name = "volume_surge";

    // Need volume history and price movement
    if (history.volumes.size() < 20 or history.prices.size() < 2)
        return signal;

    auto current_vol = history.volumes.back();
    auto avg = history.avg_volume();

    // Defensive assertions: validate volume data
    assert(current_vol >= 0 and "Volume must be non-negative");
    assert(avg >= 0 and "Average volume must be non-negative");

    if (avg == 0)
        return signal;

    auto vol_ratio = static_cast<double>(current_vol) / avg;
    assert(std::isfinite(vol_ratio) and vol_ratio >= 0.0 and "Volume ratio must be non-negative and finite");

    // Volume surge (>2x average) + upward momentum (>0.5%)
    if (vol_ratio > 2.0 and history.change_percent > 0.5) {
        signal.should_buy = true;
        signal.confidence = std::min(vol_ratio / 3.0, 1.0);  // Cap confidence at 3x volume
        signal.reason = std::format("Volume surge: {:.1f}x avg, +{:.2f}%",
                                   vol_ratio, history.change_percent);
    }

    return signal;
}

} // namespace lft
