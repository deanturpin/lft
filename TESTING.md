# LFT Testing Guide

Complete guide to testing the Low Frequency Trader system.

## Test Types

### 1. Compile-Time Tests (static_assert)

**What**: Verify constants and pure functions at compile time
**When**: Every build
**Cost**: Zero runtime overhead

**Coverage** (~160 tests):
- Parameter validation ([include/defs.h](include/defs.h))
- Exit logic calculations ([include/exit_logic_tests.h](include/exit_logic_tests.h))
- BPS conversions ([include/bps_utils.h](include/bps_utils.h))
- Helper functions ([src/phases.cxx](src/phases.cxx))

**Example**:
```cpp
static_assert(take_profit_pct >= 0.01, "Take profit too small - min 1%");
static_assert(hits_take_profit(100.0, 102.0, 0.02), "TP at +2%");
```

If a test fails, compilation stops with clear error message.

### 2. Runtime Tests (Catch2)

**What**: Verify logic with mock data and real calculations
**When**: Run manually or in CI/CD
**Coverage**: 129 assertions across 13 test cases

#### Test Suite 1: Core Functionality ([tests/test_phases.cxx](tests/test_phases.cxx))

- **Market assessment** (3 tests)
  - Empty data handling
  - Good vs wide spreads
  - Tradeable symbol detection

- **Timing helpers** (4 tests)
  - Market hours validation (9:30 AM - 4:00 PM ET)
  - Weekend detection
  - Time boundary checks

- **Configuration** (4 tests)
  - Notional amounts, calibration periods
  - Spread limits, alert thresholds

- **Alert functions** (4 tests)
  - Stock/crypto thresholds (2% vs 5%)
  - Outlier detection (20%)
  - Negative move handling

- **Watchlist** (2 tests)
  - Non-empty validation
  - Symbol format checks

#### Test Suite 2: Trading Logic ([tests/test_trading_logic.cxx](tests/test_trading_logic.cxx))

- **Exit conditions** (4 tests)
  - Take profit at 2%
  - Stop loss at 5%
  - Trailing stop at 30% from peak
  - Position holding logic

- **Spread filtering** (2 tests)
  - Narrow spreads pass (≤30 bps)
  - Wide spreads block entry (>30 bps)

- **Volume filtering** (2 tests)
  - Normal volume passes (≥50% avg)
  - Low volume blocks entry (<50% avg)

- **PriceHistory calculations** (3 tests)
  - Moving average
  - Volatility
  - Average volume

- **Cooldown enforcement** (2 tests)
  - During cooldown blocks entry
  - After cooldown allows entry

- **Strategy tests** (3 tests, hidden by default)
  - MA crossover, mean reversion, volume surge
  - Tagged with `[.]` - run with `./build/lft_trading_tests "[.]"`

## Running Tests

### Quick Test
```bash
# Build and run all tests
make && ctest --output-on-failure
```

### Individual Test Suites
```bash
# Core functionality tests
./build/lft_tests

# Trading logic tests
./build/lft_trading_tests

# Hidden strategy tests (need better mock data)
./build/lft_trading_tests "[.]"
```

### Verbose Output
```bash
# Show all test details
./build/lft_tests -s

# Show only failures
ctest --output-on-failure
```

### Filter by Tags
```bash
# Run only timing tests
./build/lft_tests "[timing]"

# Run only exit logic tests
./build/lft_trading_tests "[exit]"

# Run only filter tests
./build/lft_trading_tests "[filters]"
```

## Test Results

**Current Status**:
- ✅ Compile-time: ~160 tests passing
- ✅ Runtime: 129 assertions in 13 test cases
- ⏭️  Strategy tests: 3 tests skipped (need better mock data)

**Total**: All critical paths tested without API calls

## Adding New Tests

### Compile-Time Test
```cpp
// In any header or source file
constexpr auto my_function(double x) {
  return x * 2.0;
}

static_assert(my_function(5.0) == 10.0, "Double calculation");
```

### Runtime Test
```cpp
// In tests/test_*.cxx
TEST_CASE("Feature description", "[tag]") {
  SECTION("Specific behaviour") {
    auto result = some_function(input);
    REQUIRE(result == expected);
  }
}
```

## CI/CD Integration (Future)

```yaml
# .github/workflows/test.yml
- name: Build and Test
  run: |
    cmake -S . -B build
    cmake --build build -j4
    cd build && ctest --output-on-failure
```

## Coverage Goals

**Covered**:
- ✅ Configuration validation
- ✅ Timing logic
- ✅ Exit conditions
- ✅ Entry filters (spread, volume)
- ✅ Alert thresholds
- ✅ PriceHistory calculations

**Not Covered** (would require mocks):
- ❌ API integration
- ❌ Order placement
- ❌ Position tracking state
- ❌ Strategy signal generation (partially)

**Philosophy**: Test business logic without external dependencies. Integration testing happens in paper trading mode during market hours.
