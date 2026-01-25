// Account Summary Phase
// Displays account balances and current positions with P&L

#include "lft.h"
#include "defs.h"
#include <algorithm>
#include <format>
#include <nlohmann/json.hpp>
#include <print>
#include <string>
#include <vector>

void display_account_summary(AlpacaClient &client) {
  std::println("\n💼 Account Summary:");

  // Get and display account balances
  if (auto account_result = client.get_account()) {
    auto account_json = nlohmann::json::parse(*account_result);

    const auto equity = account_json.contains("equity") and not account_json["equity"].is_null()
      ? std::stod(account_json["equity"].get<std::string>())
      : 0.0;
    const auto buying_power = account_json.contains("buying_power") and not account_json["buying_power"].is_null()
      ? std::stod(account_json["buying_power"].get<std::string>())
      : 0.0;
    const auto daytrading_buying_power = account_json.contains("daytrading_buying_power") and not account_json["daytrading_buying_power"].is_null()
      ? std::stod(account_json["daytrading_buying_power"].get<std::string>())
      : 0.0;
    const auto cash = account_json.contains("cash") and not account_json["cash"].is_null()
      ? std::stod(account_json["cash"].get<std::string>())
      : 0.0;
    const auto daytrade_count = account_json.contains("daytrade_count") and not account_json["daytrade_count"].is_null()
      ? account_json["daytrade_count"].get<int>()
      : 0;

    std::println("\n💰 Account Balances:");
    std::println("  Equity:          ${:>12.2f}", equity);
    std::println("  Cash:            ${:>12.2f}", cash);
    std::println("  Buying Power:    ${:>12.2f}", buying_power);
    std::println("  Day Trade BP:    ${:>12.2f}", daytrading_buying_power);
    std::println("  Day Trades:      {} of 3 used", daytrade_count);
  } else {
    std::println("  ⚠️  Could not fetch account information");
  }

  // Get current positions
  auto positions = client.get_positions();
  if (not positions.empty()) {
    std::println("\n📈 Current Positions:");
    auto total_pl = 0.0;
    for (const auto &pos : positions) {
      const auto pl_emoji = pos.unrealized_pl >= 0.0 ? "🟢" : "🔴";
      std::println("  {} {:7}  {:>6.0f} @ ${:<7.2f}  P&L: ${:>8.2f} ({:>+6.2f}%)",
                   pl_emoji, pos.symbol, pos.qty, pos.avg_entry_price,
                   pos.unrealized_pl, pos.unrealized_plpc * 100.0);
      total_pl += pos.unrealized_pl;
    }
    std::println("  ───────────────────────────────────────────────────────");
    const auto total_emoji = total_pl >= 0.0 ? "🟢" : "🔴";
    std::println("  {} Total Unrealised P&L: ${:>8.2f}", total_emoji, total_pl);
  } else {
    std::println("\n📈 Current Positions: None");
  }

  // Show pending orders (useful when market is closed)
  if (auto orders_result = client.get_open_orders()) {
    auto orders_json = nlohmann::json::parse(*orders_result, nullptr, false);
    if (not orders_json.is_discarded() and orders_json.is_array() and not orders_json.empty()) {
      std::println("\n⏳ Pending Orders: {}", orders_json.size());
      for (const auto &order : orders_json) {
        const auto symbol = order.value("symbol", "");
        const auto side = order.value("side", "");
        const auto status = order.value("status", "");
        std::println("  {}  {}  ({})", symbol, side, status);
      }
    }
  }
}
