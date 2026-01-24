# LFT Project Status - January 2026

## Current Phase: Live Trading (Phase 3)

**Status**: Operational and running in live mode with 15-minute bars

**Last Updated**: 2026-01-20

---

## Recent Progress

### Architecture Evolution

The system has evolved significantly from the original three-phase plan:

1. **Phase 1: Manual Trading** ✅ COMPLETE
   - Successfully integrated with Alpaca Markets API
   - Validated API connectivity and order execution

2. **Phase 2: Backtesting** ✅ COMPLETE
   - Built automatic strategy calibration using 30 days of historical data
   - Implemented realistic spread simulation
   - Strategy selection based on backtest profitability

3. **Phase 3: Automated Trading** ✅ OPERATIONAL (since Jan 15, 2026)
   - Fully automated operation with 15-minute bar signals
   - 6 concurrent trading strategies
   - Automatic EOD liquidation at 3:50 PM ET (20:50 GMT)
   - API-based state management (no local files)

### Latest Implementation (Jan 13-20, 2026)

#### Major Architectural Change: Serial (Single-Threaded) Architecture

The biggest change is the move from a planned multi-threaded architecture to a **clean serial implementation**. This decision was made for simplicity, maintainability, and to avoid concurrency bugs.

#### Key Architectural Changes

- **Serial architecture** on `feature/serial-architecture` branch (not yet merged to main)
- Single event loop in `main.cxx` handles all phases sequentially
- Migrated from 1-minute to **15-minute bars** for reduced noise
- Removed cryptocurrency trading (temporarily - simplified codebase)
- Fixed duplicate order prevention bug (73 DOGE orders incident)
- Implemented pending order tracking to prevent race conditions
- Changed EOD cutoff from 3:55 PM to 3:50 PM ET
- Refactored timezone handling to use `std::chrono::zoned_time`
- Removed all lft namespaces from codebase

#### Why Serial Architecture?

- Simpler reasoning about state (no mutex/semaphore complexity)
- No race conditions between entry and exit logic
- Easier to debug and maintain
- Market data updates are infrequent (15-minute bars), no need for parallelism
- Can always add threading later if performance becomes an issue

The `thread_poc.cxx` file remains in the codebase as reference, but the main trading logic is deliberately single-threaded.

### Current Session Architecture

#### Entry Logic: 15-minute bars aligned to :00, :15, :30, :45

- Checks market conditions every 15 minutes
- Evaluates 6 strategies against enabled symbols
- Only trades symbols not already in positions or pending orders

**Exit Logic**: Evaluated every minute at :35 seconds

- Take profit, stop loss, trailing stop checks
- Alpaca recalculates bars at :30, so :35 ensures complete data

**Risk Management**:

- Base parameters: 2% TP/SL, 0.5% trailing stop
- Adaptive adjustments based on volatility (3:1 signal-to-noise ratio)
- Volume confidence filtering
- Noise regime detection
- Spread filtering (30 bps for stocks)
- EOD force liquidation at 3:50 PM ET

---

## Trading Performance (Jan 15-20, 2026)

### First Day with 15-Min Bars (Jan 15)

| Metric | Value |
| ------ | ----- |
| **Starting Balance** | $98,255.88 |
| **Final Balance** | $98,240.57 |
| **P&L** | **-$15.31 (-0.02%)** |
| **Total Trades** | 56 orders (28 round trips) |
| **Win Rate** | 32.1% (9 wins / 28 trades) |
| **Payoff Ratio** | 1.18 (avg win $4.17 / avg loss $3.53) |
| **Profit Factor** | 0.71 (⚠️ losses > wins) |

**Best Performers**: Commodities (+$26.82 across 11 trades)

- URA (uranium): +$11.19
- SIL (silver miners): +$10.81
- UNG (natural gas): +$10.55

**Worst Performers**: Tech stocks (-$19.21 across 4 trades)

- MSFT: -$10.74
- QQQ: -$7.92
- GOOGL: -$2.78

**Key Observations**:

- Low win rate (32% vs 42% backtest target)
- Profitable only due to larger wins than losses
- Commodity bias unexplained (warrants monitoring)
- One day insufficient for statistical significance

---

## Current Code Structure

```text
src/
  main.cxx           - Event loop with 15m entries and 1m exits
  lft.cxx            - Core trading loop and calibration
  strategies.cxx     - 6 strategy implementations
  alpaca_client.cxx  - API integration
  check_entries.cxx  - Entry evaluation logic
  check_exits.cxx    - Exit management logic
  liquidate.cxx      - EOD force liquidation
  account.cxx        - Account display utilities
  globals.cxx        - Global state management

include/
  defs.h             - Trading constants and compile-time validation
  lft.h              - Main function declarations
  strategies.h       - Strategy interfaces
  alpaca_client.h    - API client interface
  exit_logic_tests.h - Compile-time exit validation
```

### Key Design Decisions

**State Management via Alpaca API**:

- All trade metadata stored in `client_order_id` field
- Format: `"strategy_name|tp:2.0|sl:-5.0|ts:0.5"`
- Recovery on restart by querying positions and order history
- Single source of truth prevents inconsistencies

**Duplicate Prevention**:

- Tracks `symbols_in_use` from both positions and pending orders
- Critical fix after 73 duplicate DOGE orders incident (Jan 13)

**Cryptocurrency Removal** (Jan 16):

- All crypto pairs disabled due to duplicate order bug
- 200+ lines of duplicated trading loop code removed
- Simplifies maintenance, crypto can be restored from git history

**Time-Based Operations**:

- Uses DST-aware timezone conversions (EDT/EST)
- `next_15_minute_bar()`: Schedules entry evaluations
- `next_minute_at_35_seconds()`: Schedules exit checks
- `eod_cutoff_time()`: Calculates 3:50 PM ET liquidation time

---

## Active Strategies

All 6 strategies evaluated every 15 minutes:

1. **Dip Buying** - Entry on 2% price drops
2. **MA Crossover** - 5-period crosses 20-period moving average
3. **Mean Reversion** - Price >2 standard deviations below MA
4. **Volatility Breakout** - Expansion from compression with volume
5. **Relative Strength** - Outperformance vs market by >0.5%
6. **Volume Surge** - 2x average volume with upward momentum >0.5%

**Calibration**: Only enables strategies profitable in 30-day backtest

**Minimum Threshold**: Requires 10+ trades in backtest to enable strategy

---

## Technology Stack

- **Language**: C++23 (std::expected, std::ranges, std::println, std::chrono::zoned_time)
- **Compiler**: Clang/GCC with -std=c++23
- **Build System**: CMake + Make
- **Dependencies**:
  - cpp-httplib (HTTP client via FetchContent)
  - nlohmann/json (JSON parsing via FetchContent)
- **API**: Alpaca Markets (paper and live trading)
- **State**: API-based (no local files)

---

## Known Issues and Open Work

### High Priority (Active Development)

- **#53** - Analyse maximum open positions during trading day
- **#52** - Add NYSE holiday detection to prevent trading on market closures
- **#51** - Make position size a percentage of account equity (not fixed $1000)
- **#47-49** - Premarket microstructure warnings and health scoring
- **#44** - Refactor to multi-threaded architecture with API-based state

### Medium Priority (Enhancement)

- **#50** - Research and potentially implement T-Wave strategy
- **#48** - Implement breakfast breakout strategy
- **#43** - New strategy ideas for evaluation
- **#41** - Add ASCII sparklines for per-position price history
- **#40** - Add ASCII chart of balance over time to live display
- **#39** - Handle non-fractionable assets (e.g., WEAT rejection)
- **#37** - Use adaptive exit functions in live trading

### Future Work

- **#36** - Research options trading integration
- **#35** - Implement Docker Hub CI/CD with release/main branch strategy
- **#34** - Migrate helper scripts from Python/Bash to Swift
- **#31** - Add portfolio history export script
- **#30** - Add WebSocket support for real-time trade updates

### Lessons Learned (see [include/lessons_learned.md](include/lessons_learned.md))

- Removed unused lft namespaces (added noise)
- Discovered critical duplicate order bug via real trading
- API-based state management superior to file-based
- 15-minute bars significantly reduce noise vs 1-minute
- One trading day insufficient for statistical validation

---

## Recent Bug Fixes

### Critical Fixes (Jan 13-15, 2026)

1. **Duplicate Order Prevention** ([BUGFIX_2026-01-13.md](BUGFIX_2026-01-13.md))
   - Root cause: Not checking pending orders, only positions
   - Impact: 73 DOGE orders placed in one session
   - Fix: Track both `/v2/positions` and `/v2/orders?status=open`

2. **EOD Liquidation Cutoff** (Jan 13-20)
   - Changed from 3:55 PM to 3:50 PM ET (20:50 GMT)
   - Ensures positions closed before market close

3. **Timezone Handling** (Jan 20)
   - Refactored to use `std::chrono::zoned_time`
   - Proper DST-aware EDT/EST conversions

4. **Crash on Null Notional Field** (Jan 13)
   - Fixed: Check for null before accessing `order["notional"]`

---

## Configuration

### Environment Variables Required

```bash
# .env file (not in git)
ALPACA_KEY_ID=your_api_key
ALPACA_SECRET=your_secret_key
ALPACA_BASE_URL=https://paper-api.alpaca.markets  # or https://api.alpaca.markets for live
```

### Trading Parameters ([include/defs.h](include/defs.h))

```cpp
constexpr auto notional_amount = 1000.0;      // $1000 per trade
constexpr auto calibration_days = 30;         // 30-day backtest
constexpr auto min_trades_to_enable = 10;     // Min trades to enable strategy
constexpr auto max_spread_bps_stocks = 30.0;  // Max 30 bps spread
constexpr auto min_volume_ratio = 0.5;        // Min 50% of avg volume
```

---

## Deployment Status

**Current**: Running locally on macOS (development)

**Planned**: Deploy to Fasthosts VPS (similar to idapp project)

**CI/CD**: Issue #35 - Docker Hub integration planned

---

## Next Steps

### This Week

1. Monitor trading performance over 5-7 days
2. Implement session P&L display improvements
3. Add trade duration tracking (#33 if still open)
4. Validate if commodities bias continues

### This Month

1. Collect 20 days of trading data for statistical analysis
2. Compare 15-min bar performance to 1-min baseline
3. Evaluate win rate trends (target >40%)
4. Decision: Keep current architecture or adjust

### Next Quarter

1. Consider re-enabling cryptocurrency trading (after duplicate bug validation)
2. Implement multi-threaded architecture (#44)
3. Build web dashboard for monitoring
4. Deploy to production VPS

---

## Questions to Resolve

1. **Why commodities outperformed on Jan 15?**
   - Market condition or strategy bias?
   - Monitor for pattern continuation

2. **Is 32% win rate sustainable?**
   - Backtest showed 42-45% win rates
   - Need more trading days to validate

3. **Should position size be dynamic?**
   - Issue #51: Percentage of equity vs fixed $1000
   - Trade-off between simplicity and capital efficiency

4. **When to re-enable crypto?**
   - After duplicate bug thoroughly validated
   - Consider refactoring to share logic with stocks

---

## Reference Documents

- [README.md](README.md) - Project overview and quick start
- [BUILD.md](BUILD.md) - Build instructions
- [session_analysis_2026-01-15.md](session_analysis_2026-01-15.md) - First day detailed analysis
- [include/lessons_learned.md](include/lessons_learned.md) - Development lessons
- [BUGFIX_2026-01-13.md](BUGFIX_2026-01-13.md) - Duplicate order bug fix
- [METRICS.md](METRICS.md) - Performance metrics definitions
- [prioritised_strategy_improvement_actions.md](prioritised_strategy_improvement_actions.md) - Strategy enhancement roadmap
- [strategy_review.md](strategy_review.md) - Strategy evaluation framework

---

*Last Updated: 2026-01-20*
*Status: Operational - Phase 3 (Automated Trading)*
*Next Review: End of January 2026 (after 20 days of data)*
