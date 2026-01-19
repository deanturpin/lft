#include "alpaca_client.h"
#include <nlohmann/json.hpp>
#include <print>

int main() {
  std::println("🏦 Testing Account and Market Status Display\n");

  auto client = lft::AlpacaClient{};

  // Display account information
  if (auto account_result = client.get_account()) {
    auto account_json = nlohmann::json::parse(*account_result);
    const auto equity = account_json.contains("equity") and not account_json["equity"].is_null()
                            ? std::stod(account_json["equity"].get<std::string>())
                            : 0.0;
    const auto buying_power =
        account_json.contains("buying_power") and not account_json["buying_power"].is_null()
            ? std::stod(account_json["buying_power"].get<std::string>())
            : 0.0;
    const auto daytrading_buying_power =
        account_json.contains("daytrading_buying_power") and
                not account_json["daytrading_buying_power"].is_null()
            ? std::stod(account_json["daytrading_buying_power"].get<std::string>())
            : 0.0;

    std::println("💰 Account Status:");
    std::println("  Current Equity:    ${:>12.2f}", equity);
    std::println("  Buying Power:      ${:>12.2f}", buying_power);
    std::println("  Day Trade BP:      ${:>12.2f}", daytrading_buying_power);

    // Get and display market clock information
    if (auto clock_result = client.get_market_clock()) {
      const auto& clock = *clock_result;
      std::println("\n📅 Market Status:");
      std::println("  Current Time:      {}", clock.timestamp);
      std::println("  Market Open:       {}", clock.is_open ? "YES" : "NO");
      std::println("  Next Open:         {}", clock.next_open);
      std::println("  Next Close:        {}", clock.next_close);
    }
    std::println("");
  } else {
    std::println("❌ Failed to get account information");
  }

  return 0;
}
