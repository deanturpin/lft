# Out-of-Hours Price Update Analysis

## The Observation

During out-of-hours trading (evenings, weekends, pre-market), we see very infrequent price updates:
- Most assets show the same price for many consecutive polling cycles
- Occasionally one asset will update, then go quiet again
- This creates an impression of "dead" markets

## Root Cause: How Alpaca Snapshots Work

The system fetches market data using Alpaca's snapshot API:
- **Stocks**: `/v2/stocks/snapshots` endpoint
- **Crypto**: `/v1beta3/crypto/us/snapshots` endpoint

Each snapshot returns the **latest trade** that occurred:
```json
{
  "latestTrade": {
    "p": 150.23,     // Trade price
    "t": "2026-01-08T..." // Trade timestamp
  }
}
```

## Why Prices Don't Change Out of Hours

### Stock Market Behaviour
When US stock markets are closed (after 4:00 PM ET, weekends):
1. **Extended hours trading** exists but has very low volume
2. Most retail traders aren't active
3. Institutional trading is minimal
4. Some stocks may not trade at all for hours

The snapshot API returns the **most recent trade**, which during out-of-hours might be:
- Several minutes old
- The same trade for multiple consecutive polls
- From the previous trading session if no after-hours trades occurred

### Crypto Market Behaviour
Crypto trades 24/7 but experiences:
1. **Lower weekend volume** - many institutional traders offline
2. **Quieter overnight periods** - US/Europe asleep
3. **Thin markets** - fewer participants means less frequent price discovery
4. **Stable periods** - without news catalysts, prices consolidate

## Why We See "One Asset Updates Per Cycle"

This is actually **expected behaviour**:

1. **Staggered trading activity**: Different assets have different levels of liquidity
   - Popular pairs (BTC/USD, ETH/USD) trade more frequently
   - Less popular assets may have minutes between trades
   - Stocks in extended hours trade sporadically

2. **Independent market microstructure**: Each asset has its own order book
   - A trade on AAPL doesn't cause trades on MSFT
   - BTC/USD trading doesn't trigger ETH/USD trades
   - Updates appear random because they reflect actual market activity

3. **The API is working correctly**: We're seeing **real** prices from **real** trades
   - Not synthetic mid-prices or marks
   - Not interpolated values
   - The actual last executed trade

## Implications for Trading Strategy

### During Market Hours (9:30 AM - 4:00 PM ET)
- **High frequency updates**: Stocks trade continuously
- **Price discovery active**: Signals are meaningful
- **Volume provides confidence**: Our volume filters work well
- **Strategies can execute**: Liquidity is high

### Out of Hours
- **Stale prices are normal**: Lack of trades means no price changes
- **Reduced liquidity risk**: Wider spreads, harder to execute
- **Noise vs signal**: Small moves may not be actionable
- **Strategy should be conservative**: Or disable entirely

## Current System Safeguards

The system already handles this appropriately:

1. **Market status detection** ([lft.cxx:456-506](../src/lft.cxx#L456-L506)):
   ```cpp
   MarketStatus get_market_status(...) {
     // Detects weekends
     // Detects market hours (9:30 AM - 4:00 PM ET)
     // Returns time until open/close
   }
   ```

2. **Volume confidence filtering** ([lft.cxx:143-145](../src/lft.cxx#L143-L145)):
   ```cpp
   auto vol_factor = history.volume_factor();
   signal.confidence /= vol_factor; // Reduce confidence in low-volume
   ```

3. **Display shows status** ([lft.cxx:534-536](../src/lft.cxx#L534-L536)):
   - Shows market OPEN/CLOSED
   - Shows time until next open/close
   - Gives user context

## What DOES Need Fixing

### Bug: Treating Stale Trades as New Data

**Current behaviour**: Every snapshot poll adds the price to history, even if it's the same trade
```cpp
// strategies.h:60-71
void add_price(double price) {
    prices.push_back(price);  // Always pushes, even if stale!
    if (prices.size() >= 2) {
        last_price = prices[prices.size() - 2];
        change_percent = ((price - last_price) / last_price) * 100.0;
    }
}
```

**Problem**: Comparing stale trade to itself → 0% change every cycle
- Same trade from 5 minutes ago: `(150.23 - 150.23) / 150.23 = 0%`
- User sees "no change" when they should see "no new trade"

**Solution**: Check `latest_trade_timestamp` before adding to history
```cpp
struct PriceHistory {
    std::string last_trade_timestamp;  // Add this

    void add_trade(double price, std::string_view timestamp) {
        // Only add if this is a NEW trade
        if (timestamp != last_trade_timestamp) {
            prices.push_back(price);
            last_trade_timestamp = std::string{timestamp};

            if (prices.size() >= 2) {
                last_price = prices[prices.size() - 2];
                change_percent = ((price - last_price) / last_price) * 100.0;
                has_history = true;
            }
        }
        // If same timestamp, change_percent stays at previous value
    }
};
```

### What This Fixes

✅ **Accurate change tracking**: Only compute % change when price actually changes
✅ **Visual feedback**: User can see when markets are genuinely quiet (no updates)
✅ **Strategy signals**: Won't generate false signals on replayed stale trades
✅ **True market picture**: Distinguish "no movement" from "no trading"

### Implementation Changes Required

**1. Update `PriceHistory` struct** ([strategies.h:51-71](../include/strategies.h#L51-L71)):
```cpp
struct PriceHistory {
    std::vector<double> prices;
    std::vector<double> highs;
    std::vector<double> lows;
    std::vector<long> volumes;
    double last_price{};
    double change_percent{};
    bool has_history{false};
    std::string last_trade_timestamp;  // ADD THIS

    // Rename and update signature
    void add_price_with_timestamp(double price, std::string_view timestamp) {
        // Only add if this is a NEW trade
        if (timestamp.empty() || timestamp != last_trade_timestamp) {
            prices.push_back(price);
            last_trade_timestamp = std::string{timestamp};

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

    // Keep old add_price() for backtest compatibility (no timestamps in bars)
    void add_price(double price) {
        prices.push_back(price);
        if (prices.size() > 100)
            prices.erase(prices.begin());

        if (prices.size() >= 2) {
            last_price = prices[prices.size() - 2];
            change_percent = ((price - last_price) / last_price) * 100.0;
            has_history = true;
        }
    }
};
```

**2. Update `print_snapshot()` call site** ([lft.cxx:435](../src/lft.cxx#L435)):
```cpp
// OLD:
history.add_price(snap.latest_trade_price);

// NEW:
history.add_price_with_timestamp(snap.latest_trade_price,
                                  snap.latest_trade_timestamp);
```

**3. Update both stock and crypto sections** ([lft.cxx:757](../src/lft.cxx#L757) and [lft.cxx:854](../src/lft.cxx#L854)):
```cpp
// In stocks loop around line 757:
print_snapshot(symbol, snap, history);

// In crypto loop around line 854:
print_snapshot(symbol, snap, history);

// Both already call print_snapshot which calls history.add_price()
// Just need to update print_snapshot implementation
```

### Critical Consideration: Live Mode Strategy Evaluation

**The Question**: Can we safely skip updating history when we see stale trades in live mode?

**The Problem**:
- In live mode, we evaluate strategies **every cycle** (line 760-766, 857-863)
- Strategies look at `history.prices`, `history.highs`, `history.lows`
- If we skip adding stale trades, the history vectors don't grow between polls
- Some strategies may rely on having "recent" data points

**Example scenario**:
```
Cycle 1: New trade at 10:00:00 → AAPL $150.23 → history has 50 prices
Cycle 2: Stale trade (same timestamp) → Don't add → history still 50 prices
Cycle 3: Stale trade (same timestamp) → Don't add → history still 50 prices
Cycle 4: New trade at 10:03:00 → AAPL $150.45 → history now 51 prices
```

**Is this safe?**
YES, because:
1. **Moving averages** - MA(20) still works with 50 prices, doesn't need exactly 51
2. **Volatility** - Calculated from existing prices, not dependent on array length
3. **Change percent** - We preserve the last calculated value between polls
4. **Relative strength** - Compares current price to history, doesn't need exact count

**Actually, it's BETTER**:
- ✅ No duplicate prices polluting MAs
- ✅ Volatility calculations reflect actual price variation
- ✅ Volume analysis isn't diluted by repeated readings
- ✅ Strategies see the true market state

**What DOES change**:
- `prices.size()` won't increment every cycle
- But strategies should check `has_history` not array length
- The "last N prices" represent actual distinct trades, not poll cycles

## What Does NOT Need Fixing

❌ **Don't cache or interpolate prices**
- Would create synthetic prices that don't reflect reality
- Could trigger false signals on stale data
- Would hide real market conditions

❌ **Don't poll more frequently out of hours**
- Won't make markets more active
- Just wastes API quota
- No new data to fetch

## Potential Enhancements (Optional)

### 1. Show Trade Age and Update Indicator
Display how old each price is and whether it updated this cycle:
```
AAPL    150.23  (2m ago)         # No update - stale trade
TSLA    250.45  (15s ago) 🔄     # Updated this cycle - new trade
BTC/USD 42000   (now)     🔄     # Updated this cycle - fresh
```

Implementation:
```cpp
void print_snapshot(const std::string &symbol, const lft::Snapshot &snap,
                    lft::PriceHistory &history) {
    auto is_crypto = symbol.find('/') != std::string::npos;
    auto status = std::string{};
    auto colour = colour_reset;

    // Check if this is a new trade
    auto is_new_trade = snap.latest_trade_timestamp != history.last_trade_timestamp;
    auto update_indicator = is_new_trade ? " 🔄" : "";

    history.add_price_with_timestamp(snap.latest_trade_price,
                                      snap.latest_trade_timestamp);

    // Calculate trade age
    auto now = std::chrono::system_clock::now();
    auto trade_time = parse_timestamp(snap.latest_trade_timestamp);
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - trade_time);
    auto age_str = format_age(age);  // "2m ago", "15s ago", "now"

    if (history.has_history) {
        if (history.change_percent > 0.0)
            colour = colour_green;
        else if (history.change_percent < 0.0)
            colour = colour_red;

        if (is_outlier(history.change_percent))
            status = "⚠️ OUTLIER";
        else if (is_alert(history.change_percent, is_crypto))
            status = "🚨 ALERT";
    }

    std::println("{}{:<10} {:>12.2f} {:>9.2f}% {:>10} {}{}{}", colour,
                 symbol, snap.latest_trade_price, history.change_percent,
                 age_str, update_indicator, status, colour_reset);
}
```

### 2. Improve Status Line Format
Current format shows microseconds and relative cycles:
```
⏳ Next update at 14:43:25.567068 | 56 cycles until re-calibration
```

Better format with absolute times:
```
⏳ Next update: 14:43:25 | Recalibration: 15:40:00 (in 57m)
```

Implementation ([lft.cxx:947-953](../src/lft.cxx#L947-L953)):
```cpp
auto cycles_remaining = max_cycles - cycle;
auto next_update = now + std::chrono::seconds{poll_interval_seconds};
auto next_calibration = now + std::chrono::seconds{poll_interval_seconds * cycles_remaining};
auto minutes_to_calibration = cycles_remaining * poll_interval_seconds / 60;

std::println(
    "\n⏳ Next update: {:%H:%M:%S} | Recalibration: {:%H:%M:%S} (in {}m)\n",
    next_update, next_calibration, minutes_to_calibration);
```

### 3. Adjust Polling Frequency Based on Market Status
```cpp
auto poll_interval = market_status.is_open ? 60s : 300s;
// Poll every minute during market hours
// Poll every 5 minutes when closed
```

### 4. Disable Trading Out of Hours
```cpp
if (not market_status.is_open) {
  std::println("⏸ Market closed - monitoring only, no new trades");
  continue; // Skip entry logic, only manage exits
}
```

### 4. Extended Hours Flag
Add market phase detection:
```cpp
enum class MarketPhase {
  PreMarket,    // 4:00 AM - 9:30 AM ET
  Regular,      // 9:30 AM - 4:00 PM ET
  AfterHours,   // 4:00 PM - 8:00 PM ET
  Closed        // Overnight, weekends
};
```

## Conclusion

**The system has a bug** that makes quiet markets appear completely frozen.

### The Bug
Currently, the system treats every snapshot poll as "new data" even when the API returns the same stale trade. This causes:
- **False 0% changes**: Comparing a trade to itself always gives 0%
- **Hidden market activity**: When a new trade occurs, it's compared to the previous stale reading, not the last actual trade
- **Misleading display**: User sees "no change" everywhere instead of "no new trades"

### The Fix
Check `latest_trade_timestamp` before adding to price history:
1. Only update history when timestamp changes (indicates new trade)
2. Preserve last `change_percent` when no new trade occurs
3. Optionally display trade age: "AAPL 150.23 (2m ago)"

### What's Still True
Out of hours will still be quieter:
- Stocks in extended hours trade sporadically (correctly reflected)
- Crypto has lower weekend volume (correctly reflected)
- Assets trade independently (correctly reflected)

But you should see **real price movements** when they occur, not just zeros.

### Impact on Trading
This bug affects strategy signals during ALL periods, not just out of hours:
- Moving averages get polluted with duplicate prices
- Volatility calculations see false stability
- Volume analysis may be skewed

**Fix recommended** - this affects the integrity of all technical indicators.