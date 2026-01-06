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

- Currently in exploration/planning phase
- User prefers to discuss and decide before implementing
- Take time to explain options and trade-offs
