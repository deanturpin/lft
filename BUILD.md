# Building LFT

## Prerequisites

- CMake 3.25+
- C++23 compatible compiler (GCC 13+, Clang 16+, or Apple Clang 15+)
- Internet connection (for FetchContent to download dependencies)

## Quick Start

### 1. Get Alpaca API Keys

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

### 3. Build and Run

```bash
# Build and run (one command!)
make run
```

## What You Should See

###  Phase 1: Calibration

```
🤖 LFT - LOW FREQUENCY TRADER
Calibrate → Execute Workflow

🎯 CALIBRATION PHASE
Testing last 30 days of data with fixed exit parameters
Exit parameters:
  Take Profit: 1.0%
  Stop Loss: -1.0%
  Trailing Stop: 0.5%

📥 Fetching historic data
  AAPL - 28800 bars
  TSLA - 28800 bars
  ...

🔧 Testing dip...
✓ dip Complete: 1234 signals, 567 trades, $123.45 P&L, 58.2% WR
...

📊 CALIBRATION RESULTS
--------------------------------------------------------------------------------
dip                    ENABLED P&L=$  123.45 WR= 58.2%
ma_crossover          DISABLED P&L=$  -45.67 WR= 45.3%
...

2 of 5 strategies enabled for live trading
```

### Phase 2: Live Trading

```
🚀 LIVE TRADING MODE
Using calibrated exit parameters per strategy
Running for 1 hour, then will re-calibrate

Tick at 2026-01-07 10:00:00
----------------------------------------------------------------------

SYMBOL           LAST          BID          ASK    CHANGE% STATUS
----------------------------------------------------------------------
AAPL           185.23       185.22       185.24      0.12%
TSLA           242.15       242.10       242.20     -0.34%
...

📊 STRATEGY PERFORMANCE
--------------------------------------------------------------------------------------------------------------
STRATEGY           SIGNALS   EXECUTED     CLOSED       WINS   WIN RATE      NET P&L      AVG P&L
--------------------------------------------------------------------------------------------------------------
dip                      3          2          1          1      100.0%         1.23         1.23
...
```

If a strategy fires, you'll see:
```
🚨 SIGNAL: dip - Price dropped 0.21%
   Buying $100 of AAPL...
✅ Order placed
```

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

Once lft is working, you're ready to:
1. Let it run through a full calibration + trading cycle
2. Monitor the strategies that get enabled
3. Review the P&L after the 1-hour trading session
4. Deploy to a VPS for continuous operation (see [VPS_DEPLOYMENT.md](VPS_DEPLOYMENT.md))
