# LFT - Low Frequency Trader

A C++23 multi-strategy automated trading system for US stocks and crypto.

## Features

**Multi-Strategy Framework**
- 5 concurrent trading strategies evaluated each interval
- Per-strategy performance tracking with win rate and P&L metrics
- Unified position management with automated profit-taking and stop-loss
- Support for 9 stocks and 4 crypto assets

**Trading Strategies**
1. **Dip Buying** - Entry on 0.2% price drops
2. **MA Crossover** - 5-period crosses 20-period moving average
3. **Mean Reversion** - Price >2 standard deviations below MA
4. **Volatility Breakout** - Expansion from compression with volume
5. **Relative Strength** - Outperformance vs market basket by >0.5%

**Components**
- **ticker** - Live trading loop with real-time market data (60s polling)
- **backtest** - Historic strategy validation on 1-minute bars
- **positions** - Current position viewer

## Quick Start

```bash
# Configure API credentials
cp .env.example .env
# Edit .env with your Alpaca API keys

# Build and run live ticker (paper trading)
source .env
make build
make run

# Run backtesting on historic data
make backtest
```

## Tech Stack

- **Language:** C++23 (std::expected, std::ranges, std::println)
- **API:** Alpaca Markets (paper and live trading)
- **Build:** CMake + Make
- **Dependencies:** cpp-httplib, nlohmann/json (via FetchContent)

## Project Structure

```
src/
  manual/       - Live trading applications
    ticker.cxx    - Multi-strategy trading loop
    positions.cxx - Position viewer
  backtest/     - Historic validation
    backtest.cxx  - Strategy backtesting engine
  shared/       - Common libraries
    alpaca_client.cxx - API integration
    strategies.cxx    - Trading strategies
```

## Development Roadmap

- [x] Phase 1: Manual trading with Alpaca API integration
- [x] Phase 2: Backtesting framework with historic data
- [ ] Phase 3: Production deployment to VPS
- [ ] Phase 4: Real-time monitoring dashboard
