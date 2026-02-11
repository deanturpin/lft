# End of Week Trading Review Checklist

## Data Collection

- [ ] Fetch all trades for the week using `./fetch_orders.sh` for each day
- [ ] Check current account balance vs starting balance
- [ ] Verify no positions left open (check with positions API)
- [ ] Export all order data for analysis

## Performance Analysis

### Overall Metrics
- [ ] Total P&L for the week ($ and %)
- [ ] Number of trades executed
- [ ] Win rate (winning trades / total trades)
- [ ] Average profit per trade
- [ ] Largest winner and loser
- [ ] Sharpe ratio if sufficient data

### Strategy Performance
- [ ] P&L breakdown by strategy:
  - [ ] MA Crossover
  - [ ] Mean Reversion
  - [ ] Volatility Breakout
  - [ ] Relative Strength
  - [ ] Volume Surge
- [ ] Which strategies hit minimum 10 trades threshold?
- [ ] Average P&L per trade by strategy
- [ ] Strategy win rates

### Symbol Performance
- [ ] Top 10 performing symbols
- [ ] Bottom 5 performing symbols
- [ ] Which sectors performed best?
- [ ] Any symbols to add/remove from watchlist?

## Exit Parameter Comparison

### VPS Build (disabled-3-2)
- [ ] Total P&L
- [ ] Number of trades
- [ ] Average P&L per trade
- [ ] Average hold time
- [ ] Exit breakdown (trailing stop vs stop loss vs EOD)

### Laptop Build (2-1-0.9)
- [ ] Total P&L
- [ ] Number of trades
- [ ] Average P&L per trade
- [ ] Average hold time
- [ ] Exit breakdown (take profit vs trailing stop vs stop loss vs EOD)

### Comparison
- [ ] Which build performed better overall?
- [ ] Is the difference statistically significant?
- [ ] Did VPS "let winners run" capture bigger moves?
- [ ] Did laptop tight stops prevent big losses?

## Risk-Off Filter Analysis

- [ ] How many VPS trades occurred during 9:30-10:00 AM ET window?
- [ ] What was P&L of those risk-off period trades?
- [ ] Did avoiding opening volatility help or hurt?

## Issues and Anomalies

- [ ] Any network/API errors during the week?
- [ ] Any EOD liquidation failures?
- [ ] Any positions held overnight unintentionally?
- [ ] Any panic stops triggered?
- [ ] Any unexpected behaviour in strategies?

## Backtest vs Live Performance

- [ ] Compare backtest predictions to actual live results
- [ ] Are strategies performing as expected?
- [ ] Is there evidence of overfitting?
- [ ] Market regime changes this week?

## Decisions for Next Week

### Strategy Adjustments
- [ ] Enable/disable specific strategies?
- [ ] Adjust strategy parameters?
- [ ] Change minimum trades threshold?

### Exit Parameters
- [ ] Keep current A/B test (VPS vs laptop)?
- [ ] Converge both builds to winning config?
- [ ] Test new exit parameter combinations?

### Watchlist Changes
- [ ] Add new symbols based on performance?
- [ ] Remove underperforming symbols?
- [ ] Adjust sector allocation?

### Risk Management
- [ ] Adjust position sizing?
- [ ] Change stop loss levels?
- [ ] Modify panic stop threshold?

## Action Items

- [ ] Update [.claude/CLAUDE.md](file:///Users/deanturpin/lft/.claude/CLAUDE.md) with key learnings
- [ ] Document any code changes needed
- [ ] Plan configuration changes for Monday
- [ ] Set goals for next week

## Infrastructure Improvements

### Runtime Configuration (No Recompilation Needed)

- [ ] **Create config file system** - Move all parameters to external config files
  - [ ] Exit parameters (TP/SL/TS/panic) → `config/exit_params.json` or `.toml`
  - [ ] Trading parameters (notional, calibration days, etc.) → `config/trading.json`
  - [ ] Strategy enable/disable flags → `config/strategies.json`
  - [ ] Risk-off window settings → `config/risk_management.json`
  - [ ] Alert thresholds → `config/alerts.json`

- [ ] **Move symbol watchlists to files** - Read at runtime instead of compile-time
  - [ ] `watchlists/stocks.txt` - One symbol per line
  - [ ] `watchlists/crypto.txt` - Crypto pairs
  - [ ] Allow comments with `#` for documentation
  - [ ] Support grouping (e.g., `[Energy]`, `[Tech]`, etc.)

- [ ] **Benefits of runtime config:**
  - Change exit parameters without rebuilding
  - Quick A/B testing with different config files
  - Easy to swap watchlists for different market conditions
  - Can deploy same binary with different configs
  - Faster iteration and experimentation

### Implementation Notes

```text
config/
  ├── exit_params.json       # TP/SL/TS/panic settings
  ├── trading.json           # Notional, calibration, thresholds
  ├── strategies.json        # Enable/disable each strategy
  └── risk_management.json   # Risk-off windows, filters

watchlists/
  ├── stocks.txt            # Stock symbols
  ├── crypto.txt            # Crypto pairs
  ├── energy.txt            # Sector-specific (optional)
  └── tech.txt              # Sector-specific (optional)
```

Could use TOML for better readability or JSON for simplicity. Both have good C++ libraries.

## Notes

Add any observations, hypotheses, or questions here:

---

**Week of:** _________
**Starting Balance:** $_________
**Ending Balance:** $_________
**Net P&L:** $_________
**ROI:** _________%
