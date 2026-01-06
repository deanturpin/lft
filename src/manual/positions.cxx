#include "shared/alpaca_client.h"
#include <nlohmann/json.hpp>
#include <print>

using json = nlohmann::json;

int main() {
    auto client = lft::AlpacaClient{};

    if (not client.is_valid()) {
        std::println("❌ ALPACA_API_KEY and ALPACA_API_SECRET must be set");
        return 1;
    }

    std::println("Fetching open positions...\n");
    auto positions = client.get_positions();

    if (not positions) {
        std::println("❌ Failed to fetch positions");
        return 1;
    }

    try {
        auto positions_json = json::parse(positions.value());

        if (positions_json.empty()) {
            std::println("No open positions");
            return 0;
        }

        std::println("{:<10} {:>10} {:>15} {:>15} {:>15} {:>10}",
                     "SYMBOL", "QTY", "ENTRY PRICE", "CURRENT PRICE", "MARKET VALUE", "P&L %");
        std::println("{:-<85}", "");

        for (const auto& pos : positions_json) {
            auto symbol = pos["symbol"].get<std::string>();
            auto qty = pos["qty"].get<std::string>();
            auto avg_entry = std::stod(pos["avg_entry_price"].get<std::string>());
            auto current = std::stod(pos["current_price"].get<std::string>());
            auto market_value = std::stod(pos["market_value"].get<std::string>());
            auto unrealized_plpc = std::stod(pos["unrealized_plpc"].get<std::string>()) * 100.0;

            auto colour = unrealized_plpc >= 0.0 ? "\033[32m" : "\033[31m";

            std::println("{}{:<10} {:>10} {:>15.2f} {:>15.2f} {:>15.2f} {:>9.2f}%\033[0m",
                         colour,
                         symbol,
                         qty,
                         avg_entry,
                         current,
                         market_value,
                         unrealized_plpc);
        }

    } catch (const json::exception& e) {
        std::println("❌ Failed to parse positions: {}", e.what());
        return 1;
    }

    return 0;
}
