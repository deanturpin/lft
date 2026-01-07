// Compile-time tests for exit logic
// These tests verify the fundamental P&L and exit calculations are correct

namespace {

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
constexpr bool is_trailing_stop(double peak_price, double current_price,
                                 double trailing_pct) {
  auto trailing_stop_price = peak_price * (1.0 - trailing_pct);
  return current_price < trailing_stop_price;
}

// Apply spread to mid price
constexpr double apply_spread(double mid_price, double spread_pct,
                               bool buying) {
  auto half_spread = mid_price * (spread_pct / 2.0);
  return buying ? mid_price + half_spread : mid_price - half_spread;
}

// Basic P&L calculation tests
static_assert(calc_pl_pct(100.0, 102.0) == 0.02, "2% gain calculation");
static_assert(calc_pl_pct(100.0, 98.0) == -0.02, "-2% loss calculation");
static_assert(calc_pl_pct(100.0, 100.0) == 0.0, "No change calculation");

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
static_assert(is_trailing_stop(105.0, 103.94, 1_pc),
              "Trailing stop at 1% below peak");
static_assert(is_trailing_stop(105.0, 103.0, 1_pc),
              "Trailing stop >1% below peak");
static_assert(!is_trailing_stop(105.0, 103.96, 1_pc),
              "No trailing stop <1% below peak");
static_assert(!is_trailing_stop(105.0, 105.0, 1_pc),
              "No trailing stop at peak");

// Spread application tests (2 basis points = 0.02%)
static_assert(apply_spread(100.0, stock_spread, true) == 100.01,
              "Buy stock at ask (mid + half spread)");
static_assert(apply_spread(100.0, stock_spread, false) == 99.99,
              "Sell stock at bid (mid - half spread)");

// Crypto spread tests (10 basis points = 0.1%)
static_assert(apply_spread(100.0, crypto_spread, true) == 100.05,
              "Buy crypto at ask");
static_assert(apply_spread(100.0, crypto_spread, false) == 99.95,
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
static_assert(is_take_profit(100.0, 102.0, 2_pc),
              "TP triggers at exactly +2%");
static_assert(is_stop_loss(100.0, 98.0, 2_pc),
              "SL triggers at exactly -2%");

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
constexpr auto crypto_entry = apply_spread(100.0, crypto_spread, true); // 100.05
constexpr auto crypto_mid_102 = 102.0;
constexpr auto crypto_sell_102 =
    apply_spread(crypto_mid_102, crypto_spread, false); // 101.95
constexpr auto crypto_pl_102 = calc_pl_pct(crypto_entry, crypto_sell_102);

static_assert(crypto_pl_102 > 0.0189 && crypto_pl_102 < 0.0191,
              "Crypto spread reduces 2% move to ~1.9% P&L");
static_assert(!is_take_profit(crypto_entry, crypto_sell_102, 2_pc),
              "2% mid move doesn't trigger TP for crypto due to larger spread");

} // anonymous namespace

// No main function needed - this file is only for compile-time tests
// The static_asserts are evaluated at compile time
// If compilation succeeds, all tests pass!
