# LFT - Low Frequency Trader

A C++23-based algorithmic trading system for US stocks and ETFs.

## Project Goals

Build a systematic trading system in three phases:

1. **Manual Trading** - Execute individual trades to understand the API and broker mechanics
2. **Backtesting** - Develop and validate strategies using historical data
3. **Automated Trading** - Deploy proven strategies to production

## Tech Stack (In Progress)

**Core:**
- Language: C++23
- Target: US stocks and ETFs
- Trading API: Alpaca Markets (provides both trading and historical data)

**Deployment:**
- Fasthosts VPS (similar to existing idapp deployment)

**To Be Decided:**
- Web dashboard: Pure C++ HTTP server vs Node.js/TypeScript (leveraging existing expertise)
- Data storage: PostgreSQL/TimescaleDB vs file-based
- C++ libraries: HTTP client, JSON parser, WebSocket support
- Build system: CMake setup and dependencies

## Data Requirements

- Historical market data for backtesting (Alpaca Data API v2)
- Live market data for automated trading (REST and/or WebSocket)

## Development Status

Currently exploring tech stack options and API capabilities.
