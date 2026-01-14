# Outstanding Issues

Categorised list of known issues, improvements, and technical debt.

---

## CRITICAL (Production-Blocking)

None currently open.

---

## HIGH (Operational Risk)

### 1. No Duplicate Order Validation in Production
- **Status:** FIXES APPLIED, not yet validated in production
- **Impact:** Could still open multiple positions in same symbol
- **Evidence:** 6x AVAX, 3x DOGE, 3x ETH positions on 2026-01-13
- **Fix Applied:**
  - Symbol + timestamp in `client_order_id`
  - 5-minute grace period before cleanup
  - Two-layer duplicate guards
- **Action Required:** Monitor tomorrow's trading for any duplicates
- **Reference:** BUGFIX_2026-01-13.md lines 1-192

### 2. Position State Lost on Restart
- **Status:** KNOWN LIMITATION
- **Impact:** If program crashes/restarts, loses tracking of open positions
- **Risk:** Could open duplicate positions or fail to manage existing ones
- **Mitigation:** Position recovery at startup from API (60-90s lag issue)
- **Recommendation:** Persist `position_strategies` map to disk
- **Reference:** BUGFIX_2026-01-13.md line 173

### 3. No Watchdog/Health Monitoring
- **Status:** NOT IMPLEMENTED
- **Impact:** Silent failures, requires manual monitoring
- **Risk:** Could miss critical issues during trading hours
- **Recommendation:**
  - External process to monitor activity
  - Restart if no activity for 2+ minutes
  - Alert on API errors/timeouts
- **Reference:** BUGFIX_2026-01-13.md lines 325-328

---

## MEDIUM (Strategy/Performance)

### 4. Trailing Stop Too Tight (1%)
- **Status:** UNDER REVIEW
- **Evidence:** USO hit trailing stop at -1.32% loss (2026-01-13)
- **Analysis:**
  - Only 1% from peak may be too sensitive
  - Caught reversal but locked in larger loss than threshold
  - Consider: 1.5% or 2%, or adaptive based on ATR
- **Action Required:** Review after more trading days
- **Reference:** TRADING_LOG.md line 95

### 5. No Market Regime Detection
- **Status:** NOT IMPLEMENTED
- **Impact:** Strategy performs poorly in choppy/ranging markets
- **Evidence:** Death-by-a-thousand-cuts on 2026-01-13 (27% win rate)
- **Analysis:**
  - Mean reversion struggles without trend
  - 55% of exits via stop loss in ranging market
  - Need to detect range vs trend before trading
- **Recommendation:**
  - ADX (Average Directional Index) for trend strength
  - Bollinger Band width for volatility regime
  - Disable/modify strategy in unfavourable conditions
- **Reference:** TRADING_LOG.md lines 19-21, 89-103

---

## LOW (Quality of Life)

### 7. No API Health/Performance Logging
- **Status:** NOT IMPLEMENTED
- **Impact:** Can't diagnose slow/failing API calls
- **Recommendation:**
  - Log API call duration
  - Warn if >5s, error if >10s
  - Track error rates per endpoint
- **Reference:** BUGFIX_2026-01-13.md lines 315-318

### 8. No Retry Logic for Transient Failures
- **Status:** NOT IMPLEMENTED
- **Impact:** Single network hiccup fails operation
- **Recommendation:**
  - Exponential backoff retry for non-order operations
  - 3 retries with 1s, 2s, 4s delays
  - Don't retry order placement (could duplicate)
- **Reference:** BUGFIX_2026-01-13.md lines 297-308

### 9. No Circuit Breaker Pattern
- **Status:** NOT IMPLEMENTED
- **Impact:** Continues hammering API during outage
- **Recommendation:**
  - Track consecutive failures
  - Enter degraded mode after N failures
  - Skip non-critical operations, focus on position management
- **Reference:** BUGFIX_2026-01-13.md lines 310-313

### 10. Manual Trade Analysis Scripts Not Integrated
- **Status:** SCRIPTS CREATED, not integrated into workflow
- **Files:** `analyze_trades.py`, `fetch_today_trades.sh`
- **Recommendation:** Consider daily automated P&L report
- **Reference:** Git commit f127f4a

---

## FUTURE ENHANCEMENTS

### 11. Async/Non-Blocking Architecture
- **Status:** NOT PLANNED
- **Impact:** Single API hang blocks entire system (mitigated by timeouts)
- **Consideration:** Major refactor, move to thread pool
- **Benefit:** More resilient but adds complexity
- **Reference:** BUGFIX_2026-01-13.md lines 320-323

### 12. Persistent Audit Trail (SQLite)
- **Status:** NOT PLANNED
- **Impact:** Limited forensic analysis capability
- **Consideration:** Track all orders, exits, errors in database
- **Benefit:** Better debugging and strategy analysis
- **Reference:** BUGFIX_2026-01-13.md line 174

### 13. Adaptive Exit Parameters
- **Status:** NOT PLANNED
- **Impact:** Fixed 2%/2%/1% may not suit all conditions
- **Consideration:** ATR-based dynamic stops
- **Benefit:** Better risk management across different volatility regimes
- **Reference:** TRADING_LOG.md lines 122

### 14. Position Sizing Based on Volatility
- **Status:** NOT PLANNED
- **Current:** Fixed $1000 per position
- **Consideration:** Risk-adjusted sizing (e.g., lower size for volatile assets)
- **Benefit:** More consistent risk exposure

---

## VERIFICATION CHECKLIST (Next Trading Day)

Priority verification tasks for tomorrow:

- [ ] Confirm program doesn't freeze during trading hours
- [ ] Verify EOD auto-liquidation executes at 15:57 ET
- [ ] Check no duplicate orders occur
- [ ] Validate `client_order_id` shows correct parameters (tp:2.0, sl:2.0, ts:1.0)
- [ ] Monitor API timeout behaviour under normal conditions
- [ ] Review exit logic performance (TP/SL/trailing stop hit rates)
- [ ] Assess strategy performance in current market conditions

---

## PRIORITY RANKING

**Immediate (Next Session):**

1. Verify timeout fixes work (was Critical, now resolved - needs validation)
2. Monitor for duplicate orders (Issue #1 - High)
3. Validate parameter display shows correct values (Issue #1 verification)

**Short-term (This Week):**

1. Add basic health logging (Issue #7 - Low, but easy win)
2. Review trailing stop percentage after more data (Issue #4 - Medium)

**Medium-term (Next 2 Weeks):**

1. Implement market regime detection (Issue #5 - Medium)
2. Add watchdog monitoring (Issue #3 - High)
3. Implement retry logic (Issue #8 - Low)

**Long-term (Future):**

1. Adaptive exit parameters (Issue #13)
2. Circuit breaker pattern (Issue #9)
3. Persistent state to disk (Issue #2 mitigation)

---

## RESOLVED ISSUES

### ✅ HTTP Read Timeout - FIXED 2026-01-13
- Added `set_read_timeout()` to all API calls
- Commit: f127f4a

### ✅ Parameter Display Bug - FIXED 2026-01-13
- Fixed fractional percentage display in `client_order_id`
- Commit: f127f4a

### ✅ Non-Unique client_order_id - FIXED 2026-01-13
- Include symbol and timestamp for uniqueness
- Commit: 88341c2 (previous commit)

### ✅ Aggressive Cleanup Loop - FIXED 2026-01-13
- Added 5-minute grace period
- Commit: 88341c2 (previous commit)

### ✅ Missing Duplicate Guards - FIXED 2026-01-13
- Two-layer duplicate check
- Commit: 88341c2 (previous commit)

---

## NOTES

- Issues marked with ✅ are resolved but may need production validation
- Priority ranking considers both impact and implementation effort
- Market regime detection (Issue #5) is high-value but requires research
- Some "Future" items may never be needed depending on strategy evolution
