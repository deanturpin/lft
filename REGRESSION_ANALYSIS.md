# Functionality Regression Analysis: Main vs Feature/Serial-Architecture

**Date:** 2026-01-19
**Branch:** feature/serial-architecture
**Comparison:** main branch vs current branch

## Executive Summary

The serial-architecture refactoring has created a cleaner modular design but has lost approximately 70% of production functionality. The phase functions exist but the orchestration layer and safety features are incomplete.

---

## CRITICAL MISSING FEATURES (Production Blockers)

### 1. ❌ TUI (Terminal User Interface) - COMPLETELY REMOVED
**Status:** Not implemented
**Impact:** Major UX regression - no real-time monitoring

**Main branch had:**
- Full FTXUI-based interactive terminal (`src/tui.cxx`, `src/tui.hxx`)
- Real-time position tracking with visual updates
- Market quote display with signals
- Strategy performance dashboard
- Order book visualisation
- Account information with P&L tracking
- Market status countdown

**Current branch:**
- Console text output only
- No interactive interface

**Priority:** LOW (nice-to-have, not essential for trading)

---

### 2. ❌ Startup Position Recovery - MISSING
**Status:** Not implemented
**Impact:** Cannot resume after restart - loses track of open positions

**Main branch had:**
- Reads all orders from API on startup
- Reconstructs position strategies from `client_order_id`
- Recovers order IDs for tracking
- Lines 1165-1221 in main branch `lft.cxx`

**Current branch:**
- No startup recovery
- Fresh start each time

**Priority:** HIGH - Critical for production reliability

---

### 3. ❌ Duplicate Order Prevention - SIGNIFICANTLY WEAKENED
**Status:** Partially implemented
**Impact:** Risk of duplicate orders and position inconsistencies

**Main branch had THREE layers:**
1. `symbols_in_use` - from API positions and pending orders
2. `position_strategies` - local tracking map
3. 5-minute grace period to handle API lag (bug fix for race conditions)

**Current branch:**
- Basic API position check only
- No local tracking
- No grace period

**Priority:** HIGH - Critical for preventing expensive mistakes

---

### 4. ❌ Cooldown Tracking from API - MISSING
**Status:** Not implemented
**Impact:** Could re-enter positions too quickly

**Main branch:**
- Queries all recent orders on startup
- Finds most recent sell per symbol
- Calculates 15-min cooldown expiry
- Lines 894-944 in main branch

**Current branch:**
- Basic cooldown map exists in phases.cxx
- Not populated from API history

**Priority:** MEDIUM - Trading rule enforcement

---

### 5. ❌ Cost/Edge Validation - MISSING
**Status:** Not implemented
**Impact:** Could execute unprofitable trades

**Main branch:**
- Calculates expected move in bps
- Calculates total cost (spread + slippage + adverse selection)
- Requires minimum 10 bps net edge
- Blocks trades with insufficient edge
- Lines 1759-1795

**Current branch:**
- No cost/edge calculation

**Priority:** HIGH - Critical for profitability

---

### 6. ❌ EOD Liquidation - MISSING
**Status:** Not implemented
**Impact:** Could hold positions overnight unintentionally

**Main branch:**
- Closes all stock positions at 3:57 PM ET
- Keeps crypto 24/7
- Lines 1447-1496

**Current branch:**
- Code exists in main.cxx (lines 124-129)
- But uses local `eod` calculation, not API clock

**Priority:** HIGH - Risk management critical

---

### 7. ❌ Market Hours Enforcement - PARTIALLY IMPLEMENTED
**Status:** Partially working
**Impact:** Could attempt trades outside market hours

**Main branch:**
- Blocks new trades outside 9:30 AM - 3:57 PM ET
- DST handling
- Lines 1259-1262, 1699-1702

**Current branch:**
- ✅ Now checks API clock (we just implemented this!)
- ✅ Displays next open/close in local timezone
- Still needs enforcement in entry/exit logic

**Priority:** MEDIUM - Partially fixed today

---

### 8. ❌ Blocked Trade Logging - MISSING
**Status:** Not implemented
**Impact:** No audit trail for blocked trades

**Main branch:**
- Logs to CSV: timestamp, symbol, strategy, spread, volume, reason
- Lines 1086-1123

**Current branch:**
- No logging

**Priority:** LOW - Useful for analysis but not critical

---

## MEDIUM PRIORITY (Important Features)

### 9. ⚠️ Session P&L Tracking - MISSING
**Status:** Not implemented

**Main branch:**
- Captures starting equity on first call
- Shows session P&L in dollars and percentage
- Lines 1156-1158, 1026-1038

**Priority:** MEDIUM - Trader visibility

---

### 10. ⚠️ Noise Regime Filtering - MISSING
**Status:** Not implemented

**Main branch:**
- High noise → disable momentum strategies
- Low noise → disable mean reversion
- Lines 355-392

**Priority:** MEDIUM - Strategy selection sophistication

---

### 11. ⚠️ Forward Return Tracking - MISSING
**Status:** Not implemented

**Main branch:**
- Measures 10-bar forward returns for each signal
- Lines 398-428

**Priority:** LOW - Analysis feature

---

### 12. ⚠️ Parallel Calibration - REMOVED
**Status:** Sequential only

**Main branch:**
- Spawns threads for each strategy
- Lines 657-680

**Current branch:**
- Sequential loop

**Priority:** LOW - Performance optimisation

---

## LOW PRIORITY (Nice to Have)

### 13. ℹ️ Duration Statistics in Backtest
**Main branch:** Tracks min/max/avg bars per position
**Current branch:** Not implemented
**Priority:** LOW

### 14. ℹ️ Order ID Tracking
**Main branch:** Maps symbols to order IDs
**Current branch:** Not implemented
**Priority:** LOW - Minor recovery limitation

---

## IMPROVEMENTS in Feature Branch

### ✅ Better AlpacaClient API
- Convenience wrappers (`get_snapshot`, `get_bars` with days)
- Structured Position type instead of JSON string
- ✅ Market clock API with timezone conversion (implemented today!)

### ✅ Modular Phase Architecture
- Clean separation: calibrate, evaluate_entries, check_exits, liquidate_all
- Easier to understand and test

### ✅ Compile-time Validation
- More static assertions in phases.cxx

---

## Recommendations

### MUST FIX (Before Production):
1. ✅ Market clock API integration (DONE TODAY)
2. Startup position recovery
3. Three-layer duplicate order prevention with grace period
4. Cost/edge validation before entry
5. EOD liquidation (code exists, needs testing)

### SHOULD FIX (For Robustness):
6. Cooldown tracking from API
7. Session P&L tracking
8. Market hours enforcement in phases

### NICE TO HAVE (Later):
9. TUI (if desired)
10. Blocked trade logging
11. Noise regime filtering
12. Parallel calibration

---

## Next Steps

The feature branch has good architectural foundations but needs critical production features restored. Priority order:

1. **Position recovery** - Essential for restart capability
2. **Duplicate prevention** - Prevent costly errors
3. **Cost/edge validation** - Ensure profitability
4. **Testing** - Verify EOD liquidation works with API clock

The modular design makes it easier to add these features back cleanly.
