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
