#pragma once

// Compile-time tests for exit logic
// These tests verify the fundamental P&L and exit calculations are correct

namespace {

// Constexpr tolerance helper for floating-point comparisons
constexpr bool near(double a, double b, double eps = 1e-12) {
  return (a > b ? a - b : b - a) <= eps;
}

// User-defined literal for percentages
constexpr double operator""_pc(long double x) { return x / 100.0; }
constexpr double operator""_pc(unsigned long long x) { return x / 100.0; }

// Spread simulation constants (from lft.cxx)
constexpr auto stock_spread = 2.0 / 10000.0;   // 2 basis points = 0.02%
constexpr auto crypto_spread = 10.0 / 10000.0; // 10 basis points = 0.1%

// Exit parameters (from lft.cxx)
constexpr auto take_profit_pct = 2_pc;
constexpr auto stop_loss_pct = 2_pc;
constexpr auto trailing_stop_pct = 1_pc;

// Noise and signal analysis
// Minimum signal-to-noise ratio: signal must be at least 3x the noise
constexpr auto min_signal_to_noise_ratio = 3.0;

// Calculate intrabar noise from OHLC (high-low range as % of close)
constexpr double bar_noise(double high, double low, double close) {
  return (high - low) / close;
}

// Adaptive take profit: widens target when noise is high
constexpr double adaptive_take_profit(double base_tp, double noise) {
  auto min_tp = noise * min_signal_to_noise_ratio;
  return base_tp > min_tp ? base_tp : min_tp;
}

// Adaptive stop loss: widens stop when noise is high
constexpr double adaptive_stop_loss(double base_sl, double noise) {
  auto min_sl = noise * min_signal_to_noise_ratio;
  return base_sl > min_sl ? base_sl : min_sl;
}

// Calculate P&L percentage from entry and current price
constexpr double calc_pl_pct(double entry_price, double current_price) {
  return (current_price - entry_price) / entry_price;
}

// Check if take profit is triggered
constexpr bool is_take_profit(double entry_price, double current_price,
                              double tp_pct) {
  return calc_pl_pct(entry_price, current_price) >= tp_pct;
}

// Check if stop loss is triggered
constexpr bool is_stop_loss(double entry_price, double current_price,
                            double sl_pct) {
  return calc_pl_pct(entry_price, current_price) <= -sl_pct;
}

// Check if trailing stop is triggered
// Note: Uses strict inequality (<), so exactly touching the stop does NOT
// trigger
constexpr bool is_trailing_stop(double peak_price, double current_price,
                                double trailing_pct) {
  auto trailing_stop_price = peak_price * (1.0 - trailing_pct);
  return current_price < trailing_stop_price;
}

// Exit reason priority (represents actual exit decision logic)
enum class ExitReason { None, StopLoss, TakeProfit, TrailingStop };

// Determine exit reason with proper priority
// Priority order: StopLoss > TakeProfit > TrailingStop
// This reflects that protective stops should be checked first
constexpr ExitReason exit_reason(double entry_price, double peak_price,
                                 double current_price, double tp_pct,
                                 double sl_pct, double trailing_pct) {
  if (is_stop_loss(entry_price, current_price, sl_pct))
    return ExitReason::StopLoss;
  if (is_take_profit(entry_price, current_price, tp_pct))
    return ExitReason::TakeProfit;
  if (is_trailing_stop(peak_price, current_price, trailing_pct))
    return ExitReason::TrailingStop;
  return ExitReason::None;
}

// Apply spread to mid price
constexpr double apply_spread(double mid_price, double spread_pct,
                              bool buying) {
  auto half_spread = mid_price * (spread_pct / 2.0);
  return buying ? mid_price + half_spread : mid_price - half_spread;
}

// Basic P&L calculation tests
static_assert(near(calc_pl_pct(100.0, 102.0), 0.02), "2% gain calculation");
static_assert(near(calc_pl_pct(100.0, 98.0), -0.02), "-2% loss calculation");
static_assert(near(calc_pl_pct(100.0, 100.0), 0.0), "No change calculation");

// Noise calculation tests
static_assert(near(bar_noise(102.0, 98.0, 100.0), 0.04),
              "4% noise: high=102, low=98, close=100");
static_assert(near(bar_noise(100.5, 99.5, 100.0), 0.01),
              "1% noise: tight range");
static_assert(near(bar_noise(110.0, 90.0, 100.0), 0.20),
              "20% noise: very volatile bar");
static_assert(near(bar_noise(100.0, 100.0, 100.0), 0.0),
              "0% noise: no movement");

// Adaptive TP/SL tests
// Low noise (0.5%) → use base 2% TP/SL (2% > 1.5%)
static_assert(near(adaptive_take_profit(2_pc, 0.005), 2_pc),
              "Low noise: use base TP");
static_assert(near(adaptive_stop_loss(2_pc, 0.005), 2_pc),
              "Low noise: use base SL");

// High noise (1%) → use base 2% TP/SL (2% < 3%)
static_assert(adaptive_take_profit(2_pc, 0.01) > 0.029 &&
                  adaptive_take_profit(2_pc, 0.01) < 0.031,
              "High noise: widen TP to 3x noise = 3%");
static_assert(adaptive_stop_loss(2_pc, 0.01) > 0.029 &&
                  adaptive_stop_loss(2_pc, 0.01) < 0.031,
              "High noise: widen SL to 3x noise = 3%");

// Very high noise (2%) → widen significantly (2% < 6%)
static_assert(adaptive_take_profit(2_pc, 0.02) > 0.059 &&
                  adaptive_take_profit(2_pc, 0.02) < 0.061,
              "Very high noise: widen TP to 6%");
static_assert(adaptive_stop_loss(2_pc, 0.02) > 0.059 &&
                  adaptive_stop_loss(2_pc, 0.02) < 0.061,
              "Very high noise: widen SL to 6%");

// Edge case: noise exactly at threshold
// Noise = 0.67% → 3x = 2.0%, equals base
static_assert(adaptive_take_profit(2_pc, 0.0066667) > 0.0199 &&
                  adaptive_take_profit(2_pc, 0.0066667) < 0.0201,
              "Noise at threshold: use base TP");

// Realistic scenario: AAPL typical 0.3% intrabar range
// 0.3% noise → 3x = 0.9%, base 2% wins
constexpr auto aapl_noise = 0.003;
static_assert(near(adaptive_take_profit(2_pc, aapl_noise), 2_pc),
              "AAPL typical noise: use base TP");

// Volatile scenario: TSLA 1.5% intrabar range
// 1.5% noise → 3x = 4.5%, wider than base 2%
constexpr auto tsla_noise = 0.015;
static_assert(adaptive_take_profit(2_pc, tsla_noise) > 0.044 &&
                  adaptive_take_profit(2_pc, tsla_noise) < 0.046,
              "TSLA volatile: widen TP to 4.5%");

// Take profit tests (2% target)
static_assert(is_take_profit(100.0, 102.0, 2_pc), "TP at exactly 2%");
static_assert(is_take_profit(100.0, 103.0, 2_pc), "TP above 2%");
static_assert(!is_take_profit(100.0, 101.99, 2_pc), "No TP below 2%");
static_assert(!is_take_profit(100.0, 101.0, 2_pc), "No TP at 1%");

// Stop loss tests (2% limit)
static_assert(is_stop_loss(100.0, 98.0, 2_pc), "SL at exactly -2%");
static_assert(is_stop_loss(100.0, 97.0, 2_pc), "SL below -2%");
static_assert(!is_stop_loss(100.0, 98.01, 2_pc), "No SL above -2%");
static_assert(!is_stop_loss(100.0, 99.0, 2_pc), "No SL at -1%");

// Trailing stop tests (1% from peak)
// Uses strict inequality: current < threshold triggers, current == threshold
// does NOT
static_assert(is_trailing_stop(105.0, 103.94, 1_pc),
              "Trailing stop at 1% below peak");
static_assert(is_trailing_stop(105.0, 103.0, 1_pc),
              "Trailing stop >1% below peak");
static_assert(!is_trailing_stop(105.0, 103.96, 1_pc),
              "No trailing stop <1% below peak");
static_assert(!is_trailing_stop(105.0, 105.0, 1_pc),
              "No trailing stop at peak");

// Trailing stop boundary test: exactly touching the threshold does NOT trigger
constexpr auto boundary_peak = 100.0;
constexpr auto boundary_threshold =
    boundary_peak * (1.0 - trailing_stop_pct); // 99.0
static_assert(!is_trailing_stop(boundary_peak, boundary_threshold,
                                trailing_stop_pct),
              "Boundary test: exactly touching stop threshold does NOT trigger "
              "(must cross below)");

// Spread application tests (2 basis points = 0.02%)
static_assert(near(apply_spread(100.0, stock_spread, true), 100.01),
              "Buy stock at ask (mid + half spread)");
static_assert(near(apply_spread(100.0, stock_spread, false), 99.99),
              "Sell stock at bid (mid - half spread)");

// Crypto spread tests (10 basis points = 0.1%)
static_assert(near(apply_spread(100.0, crypto_spread, true), 100.05),
              "Buy crypto at ask");
static_assert(near(apply_spread(100.0, crypto_spread, false), 99.95),
              "Sell crypto at bid");

// Realistic scenario: Entry at $100, peaked at $102, now at $101
// Stock spread is 0.0002 (2 basis points = 0.02%)
// Entry: buy at ask = 100 + (100 * 0.0002 / 2) = 100 + 0.01 = 100.01
// Current: sell at bid = 101 - (101 * 0.0002 / 2) = 101 - 0.0101 = 100.9899
// P&L = (100.9899 - 100.01) / 100.01 ≈ 0.0098 = 0.98%
constexpr auto entry = apply_spread(100.0, stock_spread, true);
constexpr auto peak_mid = 102.0;
constexpr auto current_mid = 101.0;
constexpr auto sell = apply_spread(current_mid, stock_spread, false);
constexpr auto pl = calc_pl_pct(entry, sell);

// Verify spread calculations (with floating point tolerance)
static_assert(entry > 100.009 && entry < 100.011, "Entry at ask ~100.01");
static_assert(sell > 100.989 && sell < 100.991, "Exit at bid ~100.9899");
static_assert(pl > 0.0097 && pl < 0.0099, "P&L is ~0.98%");
static_assert(pl < 1_pc, "P&L below 1% trailing stop threshold");

// Verify trailing stop wouldn't trigger
// Peak at 102, trailing stop price = 102 * 0.99 = 100.98
// Current sell price = 100.9899, which is above 100.98, so no trigger
static_assert(!is_trailing_stop(peak_mid, sell, 1_pc),
              "Trailing stop not triggered when 0.99% below peak");

// Edge case: Test exact boundary conditions
static_assert(is_take_profit(100.0, 102.0, 2_pc), "TP triggers at exactly +2%");
static_assert(is_stop_loss(100.0, 98.0, 2_pc), "SL triggers at exactly -2%");

// Verify spread impact on exit conditions
// Buy stock at $100 mid → entry at $100.01 (ask)
// Price rises to $102 mid → sell at $101.99 (bid)
// P&L = (101.99 - 100.01) / 100.01 = 1.98 / 100.01 ≈ 1.98%
constexpr auto entry_100 = apply_spread(100.0, stock_spread, true);
constexpr auto mid_102 = 102.0;
constexpr auto sell_102 = apply_spread(mid_102, stock_spread, false);
constexpr auto pl_102 = calc_pl_pct(entry_100, sell_102);

static_assert(pl_102 > 0.0197 && pl_102 < 0.0200,
              "Spread reduces 2% move to ~1.98% P&L");
static_assert(!is_take_profit(entry_100, sell_102, 2_pc),
              "2% mid move doesn't trigger TP due to spread");

// What mid price movement is needed to achieve 2% P&L after spread?
// Entry at ask: 100.01
// Need sell at bid to give 2% profit: 100.01 * 1.02 = 102.0102
// If sell at bid = 102.0102, what was mid?
// bid = mid - (mid * spread / 2)
// 102.0102 = mid * (1 - 0.0001)
// mid = 102.0102 / 0.9999 ≈ 102.0204
constexpr auto target_sell = entry_100 * 1.02; // Need this at bid
constexpr auto mid_needed = target_sell / (1.0 - stock_spread / 2.0);
constexpr auto sell_needed = apply_spread(mid_needed, stock_spread, false);
constexpr auto pl_needed = calc_pl_pct(entry_100, sell_needed);

// Use tolerance for floating point comparison
static_assert(pl_needed > 0.0199, "Achieves ~2% P&L after spread");
static_assert(mid_needed > 102.02 && mid_needed < 102.03,
              "Need ~2.02% mid move");

// CRITICAL INSIGHT: Spread impact on exits
// - Entry: buy at ask = mid + spread/2
// - Exit: sell at bid = mid - spread/2
// - Total spread cost on round trip = bid-ask spread
// - For 2bp stock spread: total cost ~0.02% = 2bp
// - But expressed as % of entry price: (0.01 + 0.0101) / 100.01 ≈ 0.02%
// - To achieve 2% net P&L, mid must move ~2.02%
// - This is why effective risk/reward is slightly worse than nominal TP=SL

// Verify crypto spread has larger impact
constexpr auto crypto_entry =
    apply_spread(100.0, crypto_spread, true); // 100.05
constexpr auto crypto_mid_102 = 102.0;
constexpr auto crypto_sell_102 =
    apply_spread(crypto_mid_102, crypto_spread, false); // 101.95
constexpr auto crypto_pl_102 = calc_pl_pct(crypto_entry, crypto_sell_102);

static_assert(crypto_pl_102 > 0.0189 && crypto_pl_102 < 0.0191,
              "Crypto spread reduces 2% move to ~1.9% P&L");
static_assert(!is_take_profit(crypto_entry, crypto_sell_102, 2_pc),
              "2% mid move doesn't trigger TP for crypto due to larger spread");

// ============================================================================
// REALISTIC PRICE SEQUENCE TESTS
// Test complete trading scenarios with actual price movements
// ============================================================================

// Scenario 1: TAKE PROFIT - Stock rises to exactly trigger 2% TP
// Entry at $100 mid → buy at ask $100.01
// Need ~2.02% mid move for 2% net P&L after spread
// Price rises to $102.0306 mid → sell at bid $102.0204
// P&L = (102.0204 - 100.01) / 100.01 ≈ 2.01%
constexpr auto scenario1_entry_mid = 100.0;
constexpr auto scenario1_entry_price =
    apply_spread(scenario1_entry_mid, stock_spread, true);
constexpr auto scenario1_exit_mid = 102.0306;
constexpr auto scenario1_exit_price =
    apply_spread(scenario1_exit_mid, stock_spread, false);
constexpr auto scenario1_pl =
    calc_pl_pct(scenario1_entry_price, scenario1_exit_price);

static_assert(scenario1_entry_price > 100.009 &&
                  scenario1_entry_price < 100.011,
              "Scenario 1: Entry at ask ~100.01");
static_assert(scenario1_exit_price > 102.019 && scenario1_exit_price < 102.022,
              "Scenario 1: Exit at bid ~102.0204");
static_assert(scenario1_pl >= 0.020 && scenario1_pl < 0.0202,
              "Scenario 1: Achieves ~2% P&L");
static_assert(is_take_profit(scenario1_entry_price, scenario1_exit_price, 2_pc),
              "Scenario 1: Take profit triggers");

// Scenario 2: STOP LOSS - Stock falls to trigger -2% SL
// Entry at $100 mid → buy at ask $100.01
// Price falls to $97.9796 mid → sell at bid $97.9696
// P&L = (97.9696 - 100.01) / 100.01 = -2.0404 / 100.01 ≈ -2.04%
// Note: Loss is worse than -2% due to spread impact (buy at ask, sell at bid)
constexpr auto scenario2_entry_mid = 100.0;
constexpr auto scenario2_entry_price =
    apply_spread(scenario2_entry_mid, stock_spread, true);
constexpr auto scenario2_exit_mid = 97.9796;
constexpr auto scenario2_exit_price =
    apply_spread(scenario2_exit_mid, stock_spread, false);
constexpr auto scenario2_pl =
    calc_pl_pct(scenario2_entry_price, scenario2_exit_price);

static_assert(
    scenario2_pl < -0.0199 && scenario2_pl > -0.0210,
    "Scenario 2: Loss is ~-2.04% (spread worsens the -2% SL trigger)");
static_assert(is_stop_loss(scenario2_entry_price, scenario2_exit_price, 2_pc),
              "Scenario 2: Stop loss triggers");

// Scenario 3: TRAILING STOP - Stock peaks then falls 1% from peak
// Entry at $100 mid → buy at ask $100.01
// Peak at $105 mid
// Falls to $103.94 mid → sell at bid $103.9296
// Trailing stop price = 105 * 0.99 = 103.95
// Sell price 103.9296 < 103.95, so trailing stop triggers
// P&L = (103.9296 - 100.01) / 100.01 ≈ 3.92%
// Note: Both TP and trailing stop conditions are met, but TP has priority
constexpr auto scenario3_entry_mid = 100.0;
constexpr auto scenario3_entry_price =
    apply_spread(scenario3_entry_mid, stock_spread, true);
constexpr auto scenario3_peak_mid = 105.0;
constexpr auto scenario3_exit_mid = 103.94;
constexpr auto scenario3_exit_price =
    apply_spread(scenario3_exit_mid, stock_spread, false);
constexpr auto scenario3_pl =
    calc_pl_pct(scenario3_entry_price, scenario3_exit_price);

static_assert(scenario3_pl > 0.039 && scenario3_pl < 0.040,
              "Scenario 3: ~3.92% gain before trailing stop");
static_assert(is_trailing_stop(scenario3_peak_mid, scenario3_exit_price, 1_pc),
              "Scenario 3: Trailing stop condition met (1% below peak)");
static_assert(is_take_profit(scenario3_entry_price, scenario3_exit_price, 2_pc),
              "Scenario 3: Take profit condition also met (>2% gain)");
static_assert(exit_reason(scenario3_entry_price, scenario3_peak_mid,
                          scenario3_exit_price, take_profit_pct, stop_loss_pct,
                          trailing_stop_pct) == ExitReason::TakeProfit,
              "Scenario 3: Exit decision is TakeProfit (has priority over "
              "trailing stop)");

// Scenario 4: NEAR MISS - Stock rises to 1.99%, doesn't trigger TP
// Entry at $100 mid → buy at ask $100.01
// Price rises to $101.99 mid → sell at bid $101.9799
// P&L = (101.9799 - 100.01) / 100.01 ≈ 1.969%
constexpr auto scenario4_entry_mid = 100.0;
constexpr auto scenario4_entry_price =
    apply_spread(scenario4_entry_mid, stock_spread, true);
constexpr auto scenario4_exit_mid = 101.99;
constexpr auto scenario4_exit_price =
    apply_spread(scenario4_exit_mid, stock_spread, false);
constexpr auto scenario4_pl =
    calc_pl_pct(scenario4_entry_price, scenario4_exit_price);

static_assert(scenario4_pl > 0.0196 && scenario4_pl < 0.0198,
              "Scenario 4: ~1.97% gain");
static_assert(!is_take_profit(scenario4_entry_price, scenario4_exit_price,
                              2_pc),
              "Scenario 4: Take profit does NOT trigger (below 2%)");
static_assert(!is_stop_loss(scenario4_entry_price, scenario4_exit_price, 2_pc),
              "Scenario 4: Stop loss does NOT trigger");

// Scenario 5: CRYPTO with larger spread - 2% move insufficient
// Entry at $1000 mid → buy at ask $1000.50
// Price rises to $1020 mid → sell at bid $1019.49 (10bp = 0.1% spread)
// P&L = (1019.49 - 1000.50) / 1000.50 ≈ 1.898%
constexpr auto scenario5_entry_mid = 1000.0;
constexpr auto scenario5_entry_price =
    apply_spread(scenario5_entry_mid, crypto_spread, true);
constexpr auto scenario5_exit_mid = 1020.0;
constexpr auto scenario5_exit_price =
    apply_spread(scenario5_exit_mid, crypto_spread, false);
constexpr auto scenario5_pl =
    calc_pl_pct(scenario5_entry_price, scenario5_exit_price);

static_assert(scenario5_entry_price > 1000.49 &&
                  scenario5_entry_price < 1000.51,
              "Scenario 5: Crypto entry at ask ~1000.50");
static_assert(scenario5_exit_price > 1019.48 && scenario5_exit_price < 1019.50,
              "Scenario 5: Crypto exit at bid ~1019.49");
static_assert(scenario5_pl > 0.0189 && scenario5_pl < 0.0191,
              "Scenario 5: Crypto 2% mid move = ~1.9% P&L");
static_assert(
    !is_take_profit(scenario5_entry_price, scenario5_exit_price, 2_pc),
    "Scenario 5: Crypto 2% move insufficient for TP due to larger spread");

// Scenario 6: Trailing stop edge case - exactly at threshold
// Entry at $100 mid → buy at ask $100.01
// Peak at $105 mid
// Falls to exactly $103.95 mid → sell at bid $103.9399
// Trailing stop price = 105 * 0.99 = 103.95
// Sell price 103.9399 < 103.95, triggers (boundary condition)
constexpr auto scenario6_entry_mid = 100.0;
constexpr auto scenario6_entry_price =
    apply_spread(scenario6_entry_mid, stock_spread, true);
constexpr auto scenario6_peak_mid = 105.0;
constexpr auto scenario6_exit_mid = 103.95;
constexpr auto scenario6_exit_price =
    apply_spread(scenario6_exit_mid, stock_spread, false);

static_assert(is_trailing_stop(scenario6_peak_mid, scenario6_exit_price, 1_pc),
              "Scenario 6: Trailing stop triggers at exact boundary");

// Scenario 7: Multiple peaks - trailing stop uses highest peak
// Entry at $100, peaks at $103, falls to $102, peaks at $106, falls to $105
// Final trailing stop based on $106 peak = 106 * 0.99 = 104.94
// If price is $105 mid → sell at bid $104.9895
// 104.9895 > 104.94, so no trigger yet
// But if we used lower peak $103, threshold would be 101.97, wouldn't trigger
// either
constexpr auto scenario7_peak1 = 103.0;
constexpr auto scenario7_peak2 = 106.0;
constexpr auto scenario7_current_mid = 105.0;
constexpr auto scenario7_sell_price =
    apply_spread(scenario7_current_mid, stock_spread, false);

static_assert(!is_trailing_stop(scenario7_peak2, scenario7_sell_price, 1_pc),
              "Scenario 7: No trigger when 0.96% below peak");
static_assert(!is_trailing_stop(scenario7_peak1, scenario7_sell_price, 1_pc),
              "Scenario 7: Also no trigger from lower peak (104.99 > 101.97)");

} // anonymous namespace
