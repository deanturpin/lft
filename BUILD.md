# Building LFT

## Prerequisites

- CMake 3.25+
- C++23 compatible compiler (GCC 13+, Clang 16+, or Apple Clang 15+)
- Internet connection (for FetchContent to download dependencies)

## Quick Start

### 1. Get Alpaca Paper Trading API Keys

1. Sign up at [Alpaca Markets](https://app.alpaca.markets/signup)
2. Go to paper trading dashboard
3. Generate API keys (they're free!)

### 2. Set Environment Variables

```bash
# Copy example env file
cp .env.example .env

# Edit .env with your API keys
nano .env

# Source it
source .env
```

### 3. Build

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
cmake --build .

# Run ticker
./ticker
```

## What You Should See

```
Testing Alpaca connection...
✅ Connected to Alpaca (paper trading)

Monitoring 5 symbols (polling every 60s, alert threshold: 2.0%)
Press Ctrl+C to stop

⏰ Update at 2026-01-06 12:34:56

SYMBOL           LAST          BID          ASK    CHANGE% STATUS
----------------------------------------------------------------------
AAPL           185.23       185.22       185.24      0.00%
TSLA           242.15       242.10       242.20      0.00%
NVDA           495.80       495.75       495.85      0.00%
BTC/USD      42350.50     42348.00     42353.00      0.00%
ETH/USD       2245.80      2245.20      2246.40      0.00%
```

If a symbol moves >2% in a minute, you'll see 🚨 ALERT.

## Troubleshooting

**"ALPACA_API_KEY and ALPACA_API_SECRET must be set"**
- Make sure you ran `source .env`

**Network errors**
- Check your internet connection
- Verify API keys are correct
- Ensure you're using paper trading URL

**Compilation errors**
- Check C++23 support: `g++ --version` or `clang++ --version`
- Make sure CMake is 3.25+: `cmake --version`

## Next Steps

Once ticker is working, you're ready to:
1. Explore the Alpaca API more deeply
2. Add order placement (buy/sell)
3. Start building backtesting infrastructure
