# LFT Project Context

## Project Overview

Low Frequency Trader - algorithmic trading system for US stocks and ETFs built in C++23.

## Three-Phase Approach

1. **Manual Trading** - Learn the backend by executing trades manually via API
2. **Backtesting** - Develop strategies using historical data
3. **Automated Trading** - Deploy validated strategies to production

## Tech Stack Decisions

**Confirmed:**

- Language: C++23
- Market: US stocks and ETFs
- Trading API: Alpaca Markets
- Data needs: Both historical and live
- Deployment: Fasthosts VPS (user has existing VPS similar to idapp project)
- Build approach: Open to using C++ libraries for HTTP, JSON, WebSocket

**Under Discussion:**

- Web dashboard implementation (C++ vs Python vs TypeScript/Node.js)
- Data storage strategy (database vs files)
- Specific C++ libraries and dependencies
- Build system configuration

## Reference Projects

- `/Users/deanturpin/idapp` - User's existing TypeScript/Node.js project deployed to Fasthosts VPS
- User is comfortable with TypeScript web development but wants core trading logic in C++

## Development Style

- User prefers to discuss and decide before implementing
- Take time to explain options and trade-offs
- **Auto-commit significant changes** per global CLAUDE.md, but user said "no commits until further notice" during Feb 4 session - check context

## Helper Scripts

Always use existing scripts in `bin/` directory instead of running API calls directly:

- `bin/fetch_today_trades.sh` - Fetch today's closed orders with P&L summary
- `bin/fetch_orders.sh [date]` - Fetch orders for specific date
- `bin/post_match_analysis.sh` - Post-session analysis
- `fetch_orders.sh [date]` - Simple order fetch (defaults to today)
- `backtest_exit_params.sh` - Test different TP/SL combinations

To check positions: Use curl with proper env loading:

```bash
set -a && source .env && set +a && curl -s "https://paper-api.alpaca.markets/v2/positions" \
  -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
  -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}" | jq '.'
```

Note: Simple `source .env` often fails auth - use `set -a && source .env && set +a` pattern

## Known Issues

### EOD Liquidation Failures

- **Feb 2, 2026**: SAP position entered at 11:19 AM ET, failed to liquidate at 3:55 PM EOD cutoff
- **Feb 4, 2026**: SAP position entered at 11:17 AM ET, failed to liquidate at 3:55 PM EOD cutoff

Pattern: Positions are entering correctly but EOD liquidation (scheduled for 3:55 PM ET) is not executing. Possible causes:

- System not running at EOD time
- EOD liquidation logic has a bug
- System crashes before reaching EOD time

Investigation needed: Check if `check_panic_exits()` is being called at EOD cutoff time and if positions are being properly closed.
