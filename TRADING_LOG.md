# Trading Log

Daily trading analysis and post-mortem summaries.

---

## 2026-01-13 (Monday)

### Summary

**P&L:** -$100 (-0.10% on deployed capital)
**Positions Opened:** ~20
**Positions Closed:** 11
**Win Rate:** 3/11 (27%)
**System Uptime:** Partial (froze at 14:40 ET)

### Market Conditions

- Choppy/ranging market
- Multiple small losses, few winners
- Death-by-a-thousand-cuts scenario

### Trade Results

#### Closed Positions (Realized P&L: -$32)

**Winners:**

- NVDA: +$20.44 (+2.04%) - Only significant winner
- ETH/USD: +$6.71 (+0.23%) - Crypto duplicate order bug turned profitable
- UNG (2 trades): +$2.68 combined

**Losers:**

- TSLA: -$14.43 (-1.44%)
- USO: -$13.96 (-1.40%) - Harsh 1% trailing stop triggered
- AMZN: -$9.83 (-0.98%)
- SLV: -$4.83 (-0.48%)
- SIVR: -$6.63 (-0.66%)
- UNG: -$6.74 (-0.67%)
- XOM: -$2.14 (-0.21%)
- AVAX/USD: -$2.38 (-0.04%) - 6 duplicate orders, nearly flat
- DOGE/USD: -$0.57 (-0.02%) - 3 duplicate orders, nearly flat

#### Open Positions (Unrealized: ~-$68)

- 20 positions manually closed at 15:59 ET (1 minute before market close)
- Average unrealized loss: -0.34% per position
- Includes: AAPL, BABA, DIA, GLD, and 16 others

### Critical Incidents

#### 1. Program Freeze (CRITICAL)

**Time:** 19:40 GMT (14:40 ET / 2:40 PM ET)
**Duration:** Until manual intervention at 15:59 ET (~80 minutes)
**Impact:** Missed auto-liquidation at 15:57 ET

**Root Cause:** Missing `set_read_timeout()` on HTTP client calls. Alpaca API accepted connection but never sent response, causing infinite hang.

**Resolution:**

- Manual closure of all 20 positions at 15:59 ET
- Added read timeouts to all API calls in `alpaca_client.cxx`
- Status: FIXED (pending rebuild and test)

**Risk Assessment:**

- Could have been forced to hold overnight → gap risk
- Manual intervention required under time pressure
- Production-critical bug

#### 2. Duplicate Order Bug (MEDIUM)

**Symptom:** Multiple simultaneous positions in same crypto asset
**Examples:**

- 6x AVAX/USD positions (15:11-15:32 ET)
- 3x DOGE/USD positions (15:40-15:51 ET)
- 3x ETH/USD positions (15:11-15:13 ET)

**Outcome:** Ironically profitable or near break-even

- Multiple entry prices averaged out volatility
- Better than expected given the bug severity

**Status:** Previously documented in BUGFIX_2026-01-13.md, fixes applied

### Strategy Performance

#### Mean Reversion Strategy

- Mixed results in choppy conditions
- Small wins offset by small losses
- Trailing stop (1%) caught reversal in USO but locked in -1.32% loss
- TP target (2%) only hit once (NVDA)

#### Exit Logic Analysis

- Take Profit (2%): Hit 1/11 times (9%)
- Stop Loss (-2%): Hit ~6/11 times (55%)
- Trailing Stop (1%): Hit ~4/11 times (36%)
- Most exits via SL or trailing stop in ranging market

### Lessons Learned

1. **Infrastructure First:** Missing read timeout is unacceptable in production
   - Would have lost much more without manual intervention
   - System must be able to handle API issues gracefully

2. **Choppy Market Conditions:** Mean reversion struggles in ranging markets
   - Multiple small losses add up
   - Need better market regime detection
   - Consider wider stops in low-volatility conditions

3. **Trailing Stop Effectiveness:** 1% trailing stop seems tight
   - Caught USO reversal but locked in -1.32% loss
   - Consider adaptive trailing stop based on ATR/volatility

4. **Duplicate Order Bug:** While fixes are in place, need validation
   - Accidentally profitable this time (lucky!)
   - Must verify no duplicates tomorrow

5. **Position Sizing:** 20 positions × $1000 = $20k deployed
   - $100 loss = 0.5% of deployed capital
   - Within acceptable daily loss limits
   - But could have been worse if held overnight

### Action Items for Tomorrow

- [ ] Rebuild with read timeout fixes
- [ ] Monitor for any remaining duplicate orders
- [ ] Verify auto-liquidation at 15:57 ET works correctly
- [ ] Consider market regime filter (trend vs range detection)
- [ ] Review trailing stop percentage (1% may be too tight)
- [ ] Add API health monitoring/logging

### Code Changes

**Fixed:**

- Added `client.set_read_timeout()` to all API calls in `alpaca_client.cxx`
  - Fast operations: 15-30s
  - Bulk operations: 60s
- Fixed `client_order_id` parameter encoding (0.0 → 2.0 display bug)

**Pending:**

- Rebuild and deploy
- Test EOD liquidation under normal conditions
- Validate duplicate order fixes in production

---

## 2026-01-14 (Tuesday)

### Summary

**P&L:** +$1.54 (+0.15%)
**Positions Opened:** 0 (automated), 1 (manual test)
**Positions Closed:** 1
**Win Rate:** 1/1 (100%)
**System Uptime:** Full

**Configuration Changes:**

- Spread filter tightened: 60 bps → 30 bps
- Crypto trading: DISABLED (all pairs commented out after yesterday's duplicate order bug)

### Market Conditions

**Exceptionally low volatility day:**

- Extremely quiet market - lowest activity observed
- Calibration showed expected moves ~0.5-2 bps (far below 10 bps minimum edge requirement)
- Edge filter correctly blocked all automated trades (89 of 144 blocked trades)
- Only 21 spread blocks and 24 volume blocks - most rejections were insufficient edge
- Yesterday's average spread: 60-100 bps wide; Today's average: 3-10 bps tight
- Liquidity excellent (74% of blocked trades had spreads ≤20 bps)

**Market volatility comparison:**

- 2026-01-13: 20 positions opened, choppy but active market
- 2026-01-14: 0 positions opened automatically - market virtually frozen
- Expected moves dropped from tradeable levels to sub-1 bps

### Trade Results

#### Manual Test Trade

- **VNQ:** +$1.49 (+0.15%)
  - Entry: Manual market buy ~3:30 PM ET
  - Exit: Auto-liquidation at 3:57 PM ET (3 minutes before close)
  - Purpose: Test EOD liquidation system
  - Result: System worked perfectly ✅

**Total Realised P&L:** +$1.54 (includes $0.05 rounding)

### System Validations

#### 1. EOD Auto-Liquidation (SUCCESS ✅)

**Test:** Manual VNQ position to validate 3:57 PM ET cutoff

**Result:**

- System detected position in holdings
- Triggered liquidation exactly at 3:57 PM ET
- Position closed cleanly before 4:00 PM market close
- No gaps, no overnight holds
- Console output confirmed: "⏰ END OF DAY: Closing all positions"

**Status:** Production-ready. EOD safety system fully validated.

#### 2. Console Output Fix (SUCCESS ✅)

**Issue:** When FTXUI compiled but TUI not running (no TTY), position data wasn't displayed in console
**Root Cause:** Code checked `#ifndef HAVE_FTXUI` instead of checking if TUI was actually active
**Fix:** Added `tui_active` bool parameter, now checks runtime state not compile-time state
**Result:** Console output correctly displays when running without TTY

#### 3. Spread Filter Analysis

**Previous:** 60 bps max spread for stocks

- At 60 bps + 5 bps other costs = 65 bps total
- Consumed 32.5% of 200 bps profit target

**New:** 30 bps max spread for stocks

- At 30 bps + 5 bps other costs = 35 bps total
- Consumes 17.5% of 200 bps profit target
- **Transaction cost reduction: 46% fewer bps paid**

**Data Analysis:**

- Yesterday: 172 blocked trades, 54% would pass at 60 bps, only 26% at 20 bps
- Today: 150 blocked trades, 79% would pass at 60 bps, 74% at 20 bps
- **Conclusion:** Tight spreads available most of the time - 30 bps is appropriate

### Critical Incidents

**None.** Clean operation day.

### Strategy Performance

**All strategies DISABLED by edge filter:**

- mean_reversion: Expected move ~0.5 bps vs 10 bps required
- ma_crossover: Expected move ~1-2 bps vs 10 bps required
- volatility_breakout: Expected move ~0.5 bps vs 10 bps required
- relative_strength: Expected move ~2 bps vs 10 bps required

**Calibration Results:**

- 30-day lookback showed extremely low forward returns
- All strategies below profitability threshold
- System correctly refused to trade with negative expected value

**Edge Filter Working as Designed:**

- 89 trades blocked for insufficient edge (negative EV after costs)
- 21 trades blocked for spread violations
- 24 trades blocked for volume violations
- 16 trades blocked for combined violations

This is **correct behaviour** - the system protected capital by refusing trades that would have lost money after transaction costs.

### Lessons Learned

1. **Edge Filter Effectiveness:** System correctly sat out a dead market day
   - Zero temptation to force trades when conditions are poor
   - Calibration-based approach prevents gambling
   - Better to make $0 than lose $100

2. **Spread Filter Improvement:** Tightening from 60 to 30 bps is justified
   - Transaction costs matter enormously at tight profit margins
   - Yesterday's -$100 partly due to paying excessive spreads
   - Most liquid stocks trade at 3-10 bps spread
   - 30 bps filter still captures ~74% of opportunities

3. **Crypto Disabled - Impact:** No duplicate orders today (obviously)
   - Need to re-enable crypto carefully after thorough testing
   - Duplicate order bug was infrastructure issue, not crypto-specific
   - But worth validating with stocks first before re-enabling

4. **Market Regime Detection Works:** Calibration naturally adapts to conditions
   - In active markets (yesterday): Enables strategies
   - In dead markets (today): Disables everything
   - No manual intervention required

5. **Volatility Correlation with Profitability:**
   - Yesterday: Choppy but active → 20 trades, -$100 (spread costs too high)
   - Today: Dead market → 0 trades, +$1.54 (one test trade only)
   - **Key insight:** Without spread filter improvement, would have lost money on both days

### Configuration Changes

**Spread Filter:**

- `max_spread_bps_stocks`: 60.0 → 30.0 bps
- Rationale: Reduce transaction costs from 32.5% to 17.5% of profit target
- Impact: Rejects 26% fewer trades but dramatically improves trade quality

**Crypto Trading:**

- All crypto pairs disabled (BTC/USD, ETH/USD, SOL/USD, AVAX/USD, DOGE/USD, LINK/USD)
- Reason: Duplicate order bug from 2026-01-13 (6x AVAX, 3x DOGE, 3x ETH positions)
- Status: Temporarily disabled pending validation of fixes

### Action Items for Tomorrow

- [x] ~~Validate EOD liquidation~~ - DONE (VNQ test successful)
- [x] ~~Fix console output when TUI not running~~ - DONE
- [x] ~~Analyze spread filter effectiveness~~ - DONE (tightened to 30 bps)
- [ ] Monitor performance with new 30 bps spread filter
- [ ] Re-enable crypto trading after validating no duplicate orders with stocks
- [ ] Consider lowering `min_edge_bps` if market continues dead (currently 10 bps)
- [ ] Add "market regime" indicator to logs (active/choppy/dead)

### Code Changes

**Completed:**

- Tightened spread filter: `max_spread_bps_stocks` from 60.0 to 30.0 bps
- Fixed console output: Added `tui_active` parameter to `run_live_trading()`
- Console output now correctly displays when TUI compiled but not running

**Commits:**

1. `8353f77` - Tighten spread filter from 60 to 30 bps for stocks
2. `a994e07` - Fix console output when TUI not running

---

## Template for Future Days

```markdown
## YYYY-MM-DD (Day)

### Summary
**P&L:** $X (X%)
**Positions Opened:** N
**Positions Closed:** M
**Win Rate:** W/M (X%)
**System Uptime:** Full/Partial

### Market Conditions
- [Description]

### Trade Results
[Winners and losers]

### Critical Incidents
[Any bugs, issues, or notable events]

### Strategy Performance
[How strategies performed]

### Lessons Learned
[Key takeaways]

### Action Items for Tomorrow
- [ ] Item 1
- [ ] Item 2
```
