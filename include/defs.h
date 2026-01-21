#pragma once

// Trading parameters
constexpr auto notional_amount = 1000.0;  // Dollar amount per trade
constexpr auto calibration_days = 30;     // Duration for strategy calibration
constexpr auto min_trades_to_enable = 10; // Minimum trades to enable strategy

// Trade eligibility filters (Tier 1 - Must Do)
constexpr auto max_spread_bps_stocks =
    30.0; // Max 30 bps (0.30%) spread for stocks
constexpr auto max_spread_bps_crypto =
    100.0;                             // Max 100 bps (1.00%) spread for crypto
constexpr auto min_volume_ratio = 0.5; // Min 50% of 20-period average volume

// Cost estimation (Tier 2 - Edge Reality)
constexpr auto slippage_buffer_bps =
    3.0; // Pessimistic slippage estimate (3 bps)
constexpr auto adverse_selection_bps = 2.0; // Adverse selection cost (2 bps)
constexpr auto min_edge_bps =
    10.0; // Minimum edge required after costs (10 bps)

// Asset watchlists
#include <string>
#include <vector>

inline const auto stocks = std::vector<std::string>{
    // Major indices (high liquidity, consistent data)
    "DIA", // Dow Jones Industrial Average
    "QQQ", // Nasdaq 100
    "SPY", // S&P 500
    // Big Tech (Magnificent Seven)
    "AAPL",  // Apple - iPhone, Mac, Services
    "AMZN",  // Amazon - E-commerce, AWS cloud
    "GOOGL", // Alphabet (Google) - Search, ads, cloud
    "META",  // Meta (Facebook) - Social media, ads
    "MSFT",  // Microsoft - Software, Azure cloud, AI
    "NVDA",  // NVIDIA - GPUs, AI chips
    "TSLA",  // Tesla - Electric vehicles, energy
    // International
    "ASML", // EU semiconductors
    "BABA", // China e-commerce
    "NVO",  // Healthcare (Denmark)
    "SAP",  // European software
    "TSM",  // Taiwan Semiconductor
    // US sectors
    "BRK.B", // Berkshire Hathaway (diversified value)
    "JNJ",   // Healthcare
    "JPM",   // Financials
    "PG",    // Consumer staples
    "XOM",   // Energy
    // Commodities - Precious metals
    "GLD",  // Gold
    "SIL",  // Silver miners
    "SLV",  // Silver (iShares)
    "SIVR", // Silver (Aberdeen)
    // Commodities - Energy
    "UNG", // Natural gas
    "URA", // Uranium miners
    "USO", // Oil (United States Oil Fund)
    // Commodities - Agriculture
    "CORN", // Corn futures tracker
    "DBA",  // Agriculture basket
    // Bonds and real estate
    "IEF", // Mid-term bonds
    "TLT", // Long-term US bonds
    "VNQ"  // Real estate
};

// TEMPORARILY DISABLED (2026-01-13): Triple AVAX positions detected
// Need to investigate why duplicate order prevention isn't working for crypto
// See BUGFIX_2026-01-13.md for details on the duplicate order bug
inline const auto crypto = std::vector<std::string>{
    // DISABLED - Major cryptocurrencies (Layer 1 blockchains)
    // "BTC/USD",  // Bitcoin - Original cryptocurrency, digital gold
    // "ETH/USD",  // Ethereum - Smart contracts, DeFi, NFTs
    // "SOL/USD",  // Solana - High-speed blockchain, low fees
    // "AVAX/USD", // Avalanche - Fast, scalable smart contracts
    // DISABLED - Meme coins (high volatility)
    // "DOGE/USD", // Dogecoin - Original meme coin
    // DISABLED - DeFi and infrastructure
    // "LINK/USD"  // Chainlink - Decentralised oracles for smart contracts
};

// Timing parameters
// Note: Actual polling is aligned to :35 past each minute (see
// sleep_until_bar_ready) Alpaca recalculates bars at :30 to include late
// trades, so :35 ensures complete data
constexpr auto max_cycles = 60; // Run for 60 minutes then re-calibrate
constexpr auto cooldown_minutes =
    15; // Minutes to wait before re-entering same symbol

// Alert thresholds
constexpr auto stock_alert_threshold = 2.0;  // Standard alert at 2% move
constexpr auto crypto_alert_threshold = 5.0; // Crypto is more volatile
constexpr auto outlier_threshold = 20.0;     // Extreme move requiring attention

// Alert helper functions
constexpr bool is_alert(double change_pct, bool is_crypto) {
  auto threshold = is_crypto ? crypto_alert_threshold : stock_alert_threshold;
  auto abs_change = (change_pct < 0.0) ? -change_pct : change_pct;
  return abs_change >= threshold;
}

constexpr bool is_outlier(double change_pct) {
  auto abs_change = (change_pct < 0.0) ? -change_pct : change_pct;
  return abs_change >= outlier_threshold;
}

// Compile-time tests for alert functions
static_assert(!is_alert(1.9, false), "Stock: 1.9% should not alert (< 2%)");
static_assert(is_alert(2.0, false), "Stock: 2.0% should alert (>= 2%)");
static_assert(is_alert(5.0, false), "Stock: 5.0% should alert");
static_assert(is_alert(-2.5, false),
              "Stock: -2.5% should alert (absolute value)");
static_assert(!is_alert(4.9, true), "Crypto: 4.9% should not alert (< 5%)");
static_assert(is_alert(5.0, true), "Crypto: 5.0% should alert (>= 5%)");
static_assert(is_alert(10.0, true), "Crypto: 10.0% should alert");
static_assert(is_alert(-7.0, true),
              "Crypto: -7.0% should alert (absolute value)");
static_assert(!is_outlier(19.9), "19.9% should not be outlier (< 20%)");
static_assert(is_outlier(20.0), "20.0% should be outlier (>= 20%)");
static_assert(is_outlier(50.0), "50.0% should be outlier");
static_assert(is_outlier(-25.0), "-25.0% should be outlier (absolute value)");
static_assert(!is_outlier(0.0), "0% should not be outlier");
static_assert(!is_alert(0.0, false), "0% should not alert (stocks)");
static_assert(!is_alert(0.0, true), "0% should not alert (crypto)");

// Compile-time safety checks
static_assert(notional_amount > 0.0, "Trade size must be positive");
static_assert(notional_amount >= 1.0, "Trade size too small - minimum $1");
static_assert(notional_amount <= 100000.0,
              "Trade size dangerously high - max $100k per trade");
static_assert(calibration_days > 0, "Calibration period must be positive");
static_assert(calibration_days >= 7,
              "Calibration period too short - minimum 7 days");
static_assert(calibration_days <= 365,
              "Calibration period too long - max 1 year");
static_assert(max_cycles > 0, "Must run at least 1 cycle");
static_assert(max_cycles <= 1440,
              "Too many cycles - max 1440 (24 hours at 1 min intervals)");
static_assert(stock_alert_threshold > 0.0,
              "Stock alert threshold must be positive");
static_assert(crypto_alert_threshold > 0.0,
              "Crypto alert threshold must be positive");
static_assert(crypto_alert_threshold >= stock_alert_threshold,
              "Crypto threshold should be >= stock threshold (more volatile)");
static_assert(outlier_threshold > crypto_alert_threshold,
              "Outlier threshold must be higher than alert threshold");
static_assert(outlier_threshold <= 100.0,
              "Outlier threshold too high - max 100%");

// Trade eligibility filter checks
static_assert(max_spread_bps_stocks > 0.0,
              "Stock spread filter must be positive");
static_assert(max_spread_bps_crypto > 0.0,
              "Crypto spread filter must be positive");
static_assert(max_spread_bps_stocks >= 5.0,
              "Stock spread filter too tight - min 5 bps");
static_assert(max_spread_bps_stocks <= 100.0,
              "Stock spread filter too loose - max 100 bps");
static_assert(max_spread_bps_crypto >= max_spread_bps_stocks,
              "Crypto spread filter should be >= stocks (less liquid)");
static_assert(max_spread_bps_crypto <= 200.0,
              "Crypto spread filter too loose - max 200 bps");
static_assert(min_volume_ratio > 0.0, "Volume ratio filter must be positive");
static_assert(min_volume_ratio <= 1.0,
              "Volume ratio filter cannot exceed 100%");

// Cost estimation checks
static_assert(slippage_buffer_bps >= 0.0, "Slippage buffer cannot be negative");
static_assert(slippage_buffer_bps <= 10.0,
              "Slippage buffer too high - max 10 bps");
static_assert(adverse_selection_bps >= 0.0,
              "Adverse selection cost cannot be negative");
static_assert(adverse_selection_bps <= 10.0,
              "Adverse selection cost too high - max 10 bps");
static_assert(min_edge_bps > 0.0, "Minimum edge must be positive");
static_assert(
    min_edge_bps >= slippage_buffer_bps + adverse_selection_bps,
    "Minimum edge should cover at least slippage + adverse selection");
static_assert(min_edge_bps <= 50.0,
              "Minimum edge too high - may block all trades");

// Sanity check: total costs should be reasonable
static_assert(slippage_buffer_bps + adverse_selection_bps <
                  max_spread_bps_stocks,
              "Total non-spread costs should be less than max spread");
static_assert(
    slippage_buffer_bps + adverse_selection_bps + max_spread_bps_stocks < 100.0,
    "Total costs (spread + slippage + adverse) exceed 100 bps - unrealistic");
