// Unit tests for LFT phases
#include "lft.h"
#include "defs.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinRel;

// Test market assessment logic
// TEMPORARILY DISABLED: assess_market_conditions now requires AlpacaClient for real bar fetching
// TODO: Either mock AlpacaClient or move these to integration tests
// TEST_CASE("Market assessment correctly identifies tradeable conditions", "[market]") {
//   SECTION("Empty snapshots returns not tradeable") {
//     auto snapshots = std::vector<Snapshot>{};
//     auto assessment = assess_market_conditions(client, snapshots);
//
//     REQUIRE_FALSE(assessment.tradeable);
//     REQUIRE(assessment.summary.find("No market data") != std::string::npos);
//   }
// }

// Test timing helpers
TEST_CASE("Timing helpers calculate correct intervals", "[timing]") {
  SECTION("is_market_hours correctly identifies market hours") {
    // Create a time point for Tuesday 10:00 AM ET (14:00 UTC)
    auto tm = std::tm{};
    tm.tm_year = 126;  // 2026
    tm.tm_mon = 0;     // January
    tm.tm_mday = 20;   // Tuesday (not weekend)
    tm.tm_hour = 10;   // 10 AM
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_wday = 2;    // Tuesday

    auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));

    REQUIRE(is_market_hours(time_point));
  }

  SECTION("is_market_hours rejects weekends") {
    // Sunday January 19, 2026 (confirmed Sunday via calendar)
    auto tm = std::tm{};
    tm.tm_year = 126;  // 2026
    tm.tm_mon = 0;     // January
    tm.tm_mday = 18;   // Sunday (mktime will calculate wday)
    tm.tm_hour = 10;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;  // Let mktime determine DST

    auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));

    REQUIRE_FALSE(is_market_hours(time_point));
  }

  SECTION("is_market_hours rejects before 9:30 AM") {
    auto tm = std::tm{};
    tm.tm_year = 126;
    tm.tm_mon = 0;
    tm.tm_mday = 20;   // Tuesday
    tm.tm_hour = 9;
    tm.tm_min = 15;    // 9:15 AM - before market open
    tm.tm_sec = 0;
    tm.tm_wday = 2;

    auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));

    REQUIRE_FALSE(is_market_hours(time_point));
  }

  SECTION("is_market_hours rejects after 4:00 PM") {
    auto tm = std::tm{};
    tm.tm_year = 126;
    tm.tm_mon = 0;
    tm.tm_mday = 20;   // Tuesday
    tm.tm_hour = 16;   // 4:00 PM - market closed
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_wday = 2;

    auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));

    REQUIRE_FALSE(is_market_hours(time_point));
  }
}

// Test configuration constants
TEST_CASE("Trading constants are within safe ranges", "[config]") {
  SECTION("Notional amount is reasonable") {
    REQUIRE(notional_amount >= 100.0);
    REQUIRE(notional_amount <= 10000.0);
  }

  SECTION("Calibration period is sensible") {
    REQUIRE(calibration_days >= 7);
    REQUIRE(calibration_days <= 365);
  }

  SECTION("Spread limits are appropriate") {
    REQUIRE(max_spread_bps_stocks >= 5.0);
    REQUIRE(max_spread_bps_stocks <= 100.0);
    REQUIRE(max_spread_bps_crypto >= max_spread_bps_stocks);
  }

  SECTION("Alert thresholds are ordered correctly") {
    REQUIRE(stock_alert_threshold > 0.0);
    REQUIRE(crypto_alert_threshold >= stock_alert_threshold);
    REQUIRE(outlier_threshold > crypto_alert_threshold);
  }
}

// Test alert helper functions
TEST_CASE("Alert functions correctly identify significant moves", "[alerts]") {
  SECTION("Stock alerts trigger at 2%") {
    REQUIRE_FALSE(is_alert(1.9, false));
    REQUIRE(is_alert(2.0, false));
    REQUIRE(is_alert(3.0, false));
  }

  SECTION("Crypto alerts trigger at 5%") {
    REQUIRE_FALSE(is_alert(4.9, true));
    REQUIRE(is_alert(5.0, true));
    REQUIRE(is_alert(7.0, true));
  }

  SECTION("Outliers trigger at 20%") {
    REQUIRE_FALSE(is_outlier(19.9));
    REQUIRE(is_outlier(20.0));
    REQUIRE(is_outlier(50.0));
  }

  SECTION("Negative moves also trigger alerts") {
    REQUIRE(is_alert(-2.5, false));
    REQUIRE(is_alert(-7.0, true));
    REQUIRE(is_outlier(-25.0));
  }
}

// Test watchlist configuration
TEST_CASE("Watchlists are properly configured", "[watchlist]") {
  SECTION("Stock watchlist is not empty") {
    REQUIRE_FALSE(stocks.empty());
  }

  SECTION("Crypto watchlist exists") {
    // Even if disabled, the vector should exist
    REQUIRE(crypto.empty());  // Currently disabled in dev config
  }

  SECTION("Stock symbols are reasonable") {
    for (const auto& symbol : stocks) {
      REQUIRE_FALSE(symbol.empty());
      REQUIRE(symbol.length() <= 6);  // Most stock symbols are 1-5 chars
    }
  }
}
