# LFT Issues Review and Prioritization

Last updated: 2026-01-15

## Priority 1: Critical - Do This Week

### Validation and Analysis
- **#33 - Track trade duration in backtests** 🔥
  - **Why**: Just migrated to 15-min bars, need to verify trades are intraday
  - **Impact**: Critical for validating new strategy
  - **Effort**: Medium (add tracking to backtest loop)
  - **Blockers**: None
  - **Timeline**: This week (tonight/tomorrow)

### Deployment Infrastructure
- **#35 - Docker Hub CI/CD with release/main strategy** 🔥
  - **Why**: Enable hot-fixes during trading hours, proper deployment pipeline
  - **Impact**: High - enables rapid iteration and rollback
  - **Effort**: Medium (weekend setup)
  - **Blockers**: None
  - **Timeline**: This weekend

## Priority 2: Important - Do This Month

### Analysis and Insights
- **#31 - Portfolio history export script**
  - **Why**: Need better post-session analysis tools
  - **Impact**: Medium - improves daily review process
  - **Effort**: Low (simple script)
  - **Blockers**: None
  - **Timeline**: Next week

- **#24 - Check asymmetry of losses (detect negative edge)**
  - **Why**: Validate strategies have positive expectancy
  - **Impact**: High - could reveal losing strategies
  - **Effort**: Medium (statistical analysis)
  - **Blockers**: Need #33 done first (trade duration data)
  - **Timeline**: After #33

### Strategy Improvements
- **#19 - Add cooldowns and trade limits**
  - **Why**: Saw immediate re-entry after stop loss today (UNG example)
  - **Impact**: High - prevents overtrading and revenge trading
  - **Effort**: Medium (add cooldown tracking)
  - **Blockers**: None
  - **Timeline**: After validating current strategy works

- **#23 - Add time-based exits (especially mean reversion)**
  - **Why**: Prevent positions from sitting too long
  - **Impact**: Medium - complements existing exits
  - **Effort**: Low (add time check to exit logic)
  - **Blockers**: Need #33 to see current durations
  - **Timeline**: After #33

### Developer Experience
- **#34 - Migrate helper scripts to Swift**
  - **Why**: You prefer Swift, more likely to contribute
  - **Impact**: Medium - improves maintainability for you
  - **Effort**: Medium (port 3 scripts)
  - **Blockers**: None
  - **Timeline**: Weekend project

## Priority 3: Nice to Have - Future

### Real-time Features
- **#30 - WebSocket support for real-time updates**
  - **Why**: Reduce latency vs polling every 60 seconds
  - **Impact**: Low - 60 second polling works fine
  - **Effort**: High (new architecture)
  - **Blockers**: Current approach working
  - **Timeline**: Only if polling becomes bottleneck

### Code Quality
- **#29 - Trailing underscore for member variables**
  - **Why**: Style consistency
  - **Impact**: Low - cosmetic
  - **Effort**: Low (find/replace + review)
  - **Blockers**: None
  - **Timeline**: Slow burn, do when touching code

- **#27 - Refactor process_bar (too many parameters)**
  - **Why**: Code maintainability
  - **Impact**: Low - current code works
  - **Effort**: Medium (refactor + test)
  - **Blockers**: None
  - **Timeline**: When touching that code

- **#26 - Standardize logical operators**
  - **Why**: Style consistency (and/or vs &&/||)
  - **Impact**: Low - both work fine
  - **Effort**: Low (find/replace)
  - **Blockers**: CLAUDE.md says "prefer and/or"
  - **Timeline**: Slow burn

- **#28 - Improve DST calculation**
  - **Why**: More robust date handling
  - **Impact**: Low - only breaks twice a year
  - **Effort**: Low
  - **Blockers**: None
  - **Timeline**: Next DST change (March 2026)

### Strategy Research
- **#22 - Define stops in bps/ATR (not price levels)**
  - **Why**: More adaptive to volatility
  - **Impact**: Medium - could improve performance
  - **Effort**: Medium (research + backtest)
  - **Blockers**: Current fixed % stops working
  - **Timeline**: After validating baseline

- **#20 - Add regime classifier (liquidity/volatility)**
  - **Why**: Adapt to market conditions
  - **Impact**: Medium - could reduce losses in bad conditions
  - **Effort**: High (design + implement)
  - **Blockers**: Need baseline performance first
  - **Timeline**: Q2 2026

- **#9 - Whipsaw detection and mitigation**
  - **Why**: Reduce losses from choppy markets
  - **Impact**: Medium
  - **Effort**: High (research needed)
  - **Blockers**: Need to measure whipsaw frequency first
  - **Timeline**: Q2 2026

- **#5 - Noise-aware trading with volume analysis**
  - **Why**: Better signal quality
  - **Impact**: Medium
  - **Effort**: High (major strategy change)
  - **Blockers**: Current strategy needs validation
  - **Timeline**: Q2 2026

### Historical Issues (Low Priority)
- **#10 - Strategy calibration: all losing except mean_reversion**
  - **Status**: Partially resolved by 15-min bar migration
  - **Action**: Re-evaluate after 1 month of 15-min bar data
  - **Timeline**: February 2026

- **#8 - Review EOD position closure policy**
  - **Status**: 3:57 PM closure seems to work
  - **Action**: Monitor for issues
  - **Timeline**: Only if problems arise

- **#7 - LFT review**
  - **Status**: General discussion, no specific action
  - **Action**: Close or break into specific issues
  - **Timeline**: Review after 1 month

- **#1 - Migrate Clang to GCC**
  - **Status**: Low priority, Clang working fine
  - **Action**: Only if threading issues arise
  - **Timeline**: Only if needed

## Priority 4: Someday/Maybe

- **#36 - Research options trading**
  - **Timeline**: 6+ months (only after proving stock strategy)
  - **Action**: Revisit Q3 2026

## Recommended Action Plan

### This Week (Jan 15-19)
1. ✅ **Tonight**: Analyze today's 15-min bar session (#33 start)
2. **Tomorrow**: Implement trade duration tracking in backtest (#33)
3. **Rest of week**: Monitor live trading, collect data

### This Weekend (Jan 18-19)
1. **Docker deployment setup** (#35)
   - Link Docker Hub to GitHub
   - Create release branch
   - Update Makefile
   - Set up Watchtower on VPS
2. **Swift migration experiment** (#34 - optional)
   - Start with trade duration analysis in Swift

### Next Week (Jan 20-24)
1. **Portfolio export script** (#31)
2. **Re-run backtests with duration tracking** (#33 complete)
3. **Analyze trade patterns** - prepare for cooldown implementation

### End of Month (Jan 27-31)
1. **Implement cooldowns** (#19) if re-entry patterns problematic
2. **Add time-based exits** (#23) if trades sitting too long
3. **Month review**: Validate 15-min bars working well

### February 2026
1. **Loss asymmetry analysis** (#24)
2. **Review strategy performance** (#10 re-evaluation)
3. **Plan Q1 improvements** based on January data

## Issues to Consider Closing

- **#7** - Too vague, break into specific issues or close
- **#1** - Only reopen if threading problems occur
- **#8** - Working fine, close unless issues arise

## Dependencies Map

```
#33 (trade duration)
  ├─> #24 (loss asymmetry analysis)
  ├─> #23 (time-based exits)
  └─> #19 (cooldowns - informed by duration data)

#35 (Docker deployment)
  └─> Enables rapid iteration on all other issues

#34 (Swift migration)
  └─> Independent, can happen anytime

#36 (Options)
  └─> Blocked by 6 months of profitable trading
```

## Summary Statistics

- **Total open issues**: 20
- **Priority 1 (This week)**: 2
- **Priority 2 (This month)**: 6
- **Priority 3 (Future)**: 9
- **Priority 4 (Someday/Maybe)**: 1
- **Consider closing**: 2

## Notes

- Focus on **validation first** - prove 15-min bars work before adding complexity
- **Quick wins**: #31, #28, #29 are low-effort improvements
- **High impact**: #33, #35, #19 directly improve trading performance/operations
- **Research heavy**: #20, #22, #9, #5 need significant analysis before implementing
