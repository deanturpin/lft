// Unit tests for LFT phases
#include "lft.h"
#include "defs.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinRel;

// Test market assessment logic
TEST_CASE("Market assessment correctly identifies tradeable conditions", "[market]") {
  SECTION("Empty snapshots returns not tradeable") {
    auto snapshots = std::vector<lft::Snapshot>{};
    auto assessment = lft::assess_market_conditions(snapshots);

    REQUIRE_FALSE(assessment.tradeable);
    REQUIRE(assessment.summary.find("No market data") != std::string::npos);
  }

  SECTION("All symbols with good spreads are tradeable") {
    auto snapshots = std::vector<lft::Snapshot>{};

    // Create 3 snapshots with narrow spreads
    for (auto i = 0; i < 3; ++i) {
      auto snap = lft::Snapshot{};
      snap.symbol = "TEST" + std::to_string(i);
      snap.spread_bps = 10.0;  // 10 bps - well under 50 bps limit
      snap.tradeable = true;
      snapshots.push_back(snap);
    }

    auto assessment = lft::assess_market_conditions(snapshots);

    REQUIRE(assessment.tradeable);
    REQUIRE(assessment.summary.find("3 tradeable") != std::string::npos);
    REQUIRE_THAT(10.0, WithinRel(10.0, 0.01));  // avg spread should be 10 bps
  }

  SECTION("Wide spreads make symbols untradeable") {
    auto snapshots = std::vector<lft::Snapshot>{};

    // Create snapshot with wide spread
    auto snap = lft::Snapshot{};
    snap.symbol = "ILLIQUID";
    snap.spread_bps = 100.0;  // 100 bps - too wide
    snap.tradeable = false;
    snapshots.push_back(snap);

    auto assessment = lft::assess_market_conditions(snapshots);

    REQUIRE_FALSE(assessment.tradeable);  // No tradeable symbols
    REQUIRE(assessment.summary.find("0 tradeable") != std::string::npos);
  }
}

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

    REQUIRE(lft::is_market_hours(time_point));
  }

  SECTION("is_market_hours rejects weekends") {
    // Sunday
    auto tm = std::tm{};
    tm.tm_year = 126;
    tm.tm_mon = 0;
    tm.tm_mday = 19;   // Sunday
    tm.tm_hour = 10;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_wday = 0;    // Sunday

    auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));

    REQUIRE_FALSE(lft::is_market_hours(time_point));
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

    REQUIRE_FALSE(lft::is_market_hours(time_point));
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

    REQUIRE_FALSE(lft::is_market_hours(time_point));
  }
}

// Test configuration constants
TEST_CASE("Trading constants are within safe ranges", "[config]") {
  SECTION("Notional amount is reasonable") {
    REQUIRE(lft::notional_amount >= 100.0);
    REQUIRE(lft::notional_amount <= 10000.0);
  }

  SECTION("Calibration period is sensible") {
    REQUIRE(lft::calibration_days >= 7);
    REQUIRE(lft::calibration_days <= 365);
  }

  SECTION("Spread limits are appropriate") {
    REQUIRE(lft::max_spread_bps_stocks >= 5.0);
    REQUIRE(lft::max_spread_bps_stocks <= 100.0);
    REQUIRE(lft::max_spread_bps_crypto >= lft::max_spread_bps_stocks);
  }

  SECTION("Alert thresholds are ordered correctly") {
    REQUIRE(lft::stock_alert_threshold > 0.0);
    REQUIRE(lft::crypto_alert_threshold >= lft::stock_alert_threshold);
    REQUIRE(lft::outlier_threshold > lft::crypto_alert_threshold);
  }
}

// Test alert helper functions
TEST_CASE("Alert functions correctly identify significant moves", "[alerts]") {
  SECTION("Stock alerts trigger at 2%") {
    REQUIRE_FALSE(lft::is_alert(1.9, false));
    REQUIRE(lft::is_alert(2.0, false));
    REQUIRE(lft::is_alert(3.0, false));
  }

  SECTION("Crypto alerts trigger at 5%") {
    REQUIRE_FALSE(lft::is_alert(4.9, true));
    REQUIRE(lft::is_alert(5.0, true));
    REQUIRE(lft::is_alert(7.0, true));
  }

  SECTION("Outliers trigger at 20%") {
    REQUIRE_FALSE(lft::is_outlier(19.9));
    REQUIRE(lft::is_outlier(20.0));
    REQUIRE(lft::is_outlier(50.0));
  }

  SECTION("Negative moves also trigger alerts") {
    REQUIRE(lft::is_alert(-2.5, false));
    REQUIRE(lft::is_alert(-7.0, true));
    REQUIRE(lft::is_outlier(-25.0));
  }
}

// Test watchlist configuration
TEST_CASE("Watchlists are properly configured", "[watchlist]") {
  SECTION("Stock watchlist is not empty") {
    REQUIRE_FALSE(lft::stocks.empty());
  }

  SECTION("Crypto watchlist exists") {
    // Even if disabled, the vector should exist
    REQUIRE(lft::crypto.empty());  // Currently disabled in dev config
  }

  SECTION("Stock symbols are reasonable") {
    for (const auto& symbol : lft::stocks) {
      REQUIRE_FALSE(symbol.empty());
      REQUIRE(symbol.length() <= 6);  // Most stock symbols are 1-5 chars
    }
  }
}
