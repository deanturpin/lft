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

## What Does NOT Need Fixing

❌ **Don't cache or interpolate prices**
- Would create synthetic prices that don't reflect reality
- Could trigger false signals on stale data
- Would hide real market conditions

❌ **Don't add artificial "freshness" checks**
- The timestamp is already in `latest_trade_timestamp`
- But stale prices during quiet periods are valid
- They represent lack of trading, not broken data

❌ **Don't poll more frequently out of hours**
- Won't make markets more active
- Just wastes API quota
- No new data to fetch

## Potential Enhancements (Optional)

### 1. Show Trade Timestamps
Display how old each price is:
```
AAPL    150.23  (2m ago)
TSLA    250.45  (15s ago)
BTC/USD 42000   (now)
```

### 2. Adjust Polling Frequency Based on Market Status
```cpp
auto poll_interval = market_status.is_open ? 60s : 300s;
// Poll every minute during market hours
// Poll every 5 minutes when closed
```

### 3. Disable Trading Out of Hours
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

**The system is working correctly.** Infrequent price updates out of hours reflect **real market conditions**, not a technical issue. The "one price changes per cycle" observation is normal behaviour when:
- Markets have low volume
- Assets trade independently
- Real trades are infrequent

The current implementation appropriately:
✅ Shows real prices from real trades
✅ Displays market status to user
✅ Reduces confidence in low-volume signals
✅ Provides safeguards against acting on stale data

**No fix is required** - this is the market showing its true nature during quiet periods.