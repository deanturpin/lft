# LFT - Low Frequency Trader

A C++23 multi-strategy automated trading system for US stocks and crypto.

## Features

**Multi-Strategy Framework**
- 5 concurrent trading strategies evaluated each interval
- Automatic calibration on 30 days of historic data with spread simulation
- Only enables profitable strategies based on backtest results
- Per-strategy performance tracking with win rate and P&L metrics
- Automated position management with profit-taking and stop-loss
- 7 stocks and 4 crypto assets

**Trading Strategies**
1. **Dip Buying** - Entry on 2% price drops
2. **MA Crossover** - 5-period crosses 20-period moving average
3. **Mean Reversion** - Price >2 standard deviations below MA
4. **Volatility Breakout** - Expansion from compression with volume
5. **Relative Strength** - Outperformance vs market basket by >0.5%

**Exit Parameters** (1:1 risk/reward)
- Take Profit: 1%
- Stop Loss: -1%
- Trailing Stop: 0.5%

## Quick Start

```bash
# Configure API credentials
cp .env.example .env
# Edit .env with your Alpaca API keys

# Build and run (paper trading with calibration)
source .env
make run
```

## Tech Stack

- **Language:** C++23 (std::expected, std::ranges, std::println)
- **API:** Alpaca Markets (paper and live trading)
- **Build:** CMake + Make
- **Dependencies:** cpp-httplib, nlohmann/json (via FetchContent)

## Project Structure

```
src/
  lft.cxx       - Main application (calibrate + execute workflow)
  shared/       - Common libraries
    alpaca_client.cxx - Alpaca API integration
    strategies.cxx    - Trading strategy implementations
include/
  shared/       - Header files
tests/          - Test suite
```

## Development Roadmap

- [x] Phase 1: Manual trading with Alpaca API integration
- [x] Phase 2: Backtesting framework with historic data
- [ ] Phase 3: Production deployment to VPS
- [ ] Phase 4: Real-time monitoring dashboard
