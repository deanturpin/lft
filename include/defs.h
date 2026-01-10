#pragma once

namespace lft {

// Trading parameters
constexpr auto notional_amount = 1000.0;      // Dollar amount per trade
constexpr auto calibration_days = 30;         // Duration for strategy calibration
constexpr auto min_trades_to_enable = 10;     // Minimum trades to enable strategy

// Trade eligibility filters (Tier 1 - Must Do)
constexpr auto max_spread_bps_stocks = 25.0;  // Max 25 bps (0.25%) spread for stocks
constexpr auto max_spread_bps_crypto = 50.0;  // Max 50 bps (0.50%) spread for crypto
constexpr auto min_volume_ratio = 0.5;        // Min 50% of 20-period average volume

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
    "WEAT", // Wheat
    // Bonds and real estate
    "IEF", // Mid-term bonds
    "TLT", // Long-term US bonds
    "VNQ"  // Real estate
};

inline const auto crypto = std::vector<std::string>{
    // Major cryptocurrencies (Layer 1 blockchains)
    "BTC/USD",  // Bitcoin - Original cryptocurrency, digital gold
    "ETH/USD",  // Ethereum - Smart contracts, DeFi, NFTs
    "SOL/USD",  // Solana - High-speed blockchain, low fees
    "AVAX/USD", // Avalanche - Fast, scalable smart contracts
    // Meme coins (high volatility)
    "DOGE/USD", // Dogecoin - Original meme coin
    // DeFi and infrastructure
    "LINK/USD"  // Chainlink - Decentralised oracles for smart contracts
};

// Timing parameters
constexpr auto poll_interval_seconds = 65; // 60s bar + buffer
constexpr auto max_cycles = 60; // Run for 60 minutes then re-calibrate

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
static_assert(poll_interval_seconds >= 60,
              "Poll interval too short - minimum 60s for 1Min bars");
static_assert(poll_interval_seconds <= 300,
              "Poll interval too long - max 5 minutes");
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
static_assert(max_spread_bps_stocks > 0.0, "Stock spread filter must be positive");
static_assert(max_spread_bps_crypto > 0.0, "Crypto spread filter must be positive");
static_assert(max_spread_bps_stocks >= 5.0, "Stock spread filter too tight - min 5 bps");
static_assert(max_spread_bps_stocks <= 100.0, "Stock spread filter too loose - max 100 bps");
static_assert(max_spread_bps_crypto >= max_spread_bps_stocks,
              "Crypto spread filter should be >= stocks (less liquid)");
static_assert(max_spread_bps_crypto <= 200.0, "Crypto spread filter too loose - max 200 bps");
static_assert(min_volume_ratio > 0.0, "Volume ratio filter must be positive");
static_assert(min_volume_ratio <= 1.0, "Volume ratio filter cannot exceed 100%");

} // namespace lft
