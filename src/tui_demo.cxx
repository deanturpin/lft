// TUI demonstration program - shows TUI capabilities without trading
#include "tui.hxx"
#include <chrono>
#include <print>
#include <random>
#include <thread>

using namespace std::chrono_literals;

int main() {
  std::println("LFT TUI Demo - Press q to quit\n");

  // Create monitor and renderer (force TUI even without TTY for demo purposes)
  auto monitor = std::make_shared<tui::trading_monitor>();
  auto renderer = std::make_unique<tui::tui_renderer>(monitor);

  // Random number generator for demo
  std::random_device rd;
  std::mt19937 gen{rd()};
  std::uniform_real_distribution<> price_change{-2.0, 2.0};

  // Initial setup
  monitor->update_account(100000.0, 50000.0, 0uz);
  monitor->set_market_status(true, "Closes in 2h 15m");

  // Demo data
  std::vector<std::string> symbols = {"AAPL",  "NVDA", "TSLA", "AMZN",
                                      "GOOGL", "MSFT", "META",  "ETH/USD"};
  std::vector<double> base_prices = {185.0, 875.0, 245.0, 175.0,
                                     140.0, 415.0, 485.0, 3420.0};

  // Add some initial positions
  for (auto i = 0uz; i < 3uz; ++i) {
    auto entry = base_prices[i];
    auto current = entry * (1.0 + price_change(gen) / 100.0);
    auto qty = 1000.0 / entry;
    auto pnl = (current - entry) * qty;

    monitor->update_position(symbols[i], tui::position_data{
                                             .symbol = symbols[i],
                                             .strategy = "mean_reversion",
                                             .entry_price = entry,
                                             .current_price = current,
                                             .quantity = qty,
                                             .market_value = current * qty,
                                             .unrealised_pnl = pnl,
                                             .unrealised_pct = (pnl / 1000.0) * 100.0,
                                             .take_profit_pct = 2.0,
                                             .stop_loss_pct = 2.0,
                                             .trailing_stop_pct = 1.0,
                                             .entry_time = std::chrono::system_clock::now(),
                                         });
  }

  // Add market quotes
  for (auto i = 0uz; i < symbols.size(); ++i) {
    monitor->update_quote(symbols[i],
                          tui::market_quote{
                              .symbol = symbols[i],
                              .price = base_prices[i],
                              .change_pct = price_change(gen),
                              .last_signal = i % 3 == 0 ? "BUY" : "",
                              .signal_strategy = i % 3 == 0 ? "momentum" : "",
                              .has_position = i < 3,
                          });
  }

  // Add strategy stats
  monitor->update_strategy_stats("mean_reversion",
                                 tui::strategy_stats{
                                     .name = "mean_reversion",
                                     .signals_generated = 45uz,
                                     .signals_executed = 32uz,
                                     .positions_open = 3uz,
                                     .positions_closed = 29uz,
                                     .wins = 18uz,
                                     .losses = 11uz,
                                     .total_pnl = 234.50,
                                     .win_rate_pct = 62.1,
                                 });

  monitor->update_strategy_stats("momentum", tui::strategy_stats{
                                                 .name = "momentum",
                                                 .signals_generated = 38uz,
                                                 .signals_executed = 28uz,
                                                 .positions_open = 2uz,
                                                 .positions_closed = 26uz,
                                                 .wins = 20uz,
                                                 .losses = 6uz,
                                                 .total_pnl = 512.30,
                                                 .win_rate_pct = 76.9,
                                             });

  // Add some orders
  for (auto i = 0uz; i < 5uz; ++i) {
    monitor->add_order(tui::order_entry{
        .number = i + 1,
        .timestamp = std::chrono::system_clock::now() -
                     std::chrono::minutes{5 - i},
        .symbol = symbols[i],
        .side = "buy",
        .status = "filled",
        .quantity = 1000.0 / base_prices[i],
        .fill_price = base_prices[i],
        .client_order_id = std::format("{}_mr_{}", symbols[i], i),
    });
  }

  // Start TUI
  renderer->start();

  // Simulation loop - update prices periodically
  auto quit_requested = false;
  renderer->set_quit_callback([&] { quit_requested = true; });

  auto cycle = 0uz;
  while (not quit_requested) {
    // Update cycle count
    monitor->increment_cycle_count();

    // Simulate price changes
    for (auto i = 0uz; i < 3uz; ++i) {
      auto positions = monitor->get_positions();
      if (i < positions.size()) {
        auto &pos = positions[i];
        auto new_price = pos.current_price * (1.0 + price_change(gen) / 1000.0);
        auto pnl = (new_price - pos.entry_price) * pos.quantity;

        monitor->update_position(pos.symbol, tui::position_data{
                                                 .symbol = pos.symbol,
                                                 .strategy = pos.strategy,
                                                 .entry_price = pos.entry_price,
                                                 .current_price = new_price,
                                                 .quantity = pos.quantity,
                                                 .market_value = new_price * pos.quantity,
                                                 .unrealised_pnl = pnl,
                                                 .unrealised_pct = (pnl / (pos.entry_price * pos.quantity)) * 100.0,
                                                 .take_profit_pct = 2.0,
                                                 .stop_loss_pct = 2.0,
                                                 .trailing_stop_pct = 1.0,
                                                 .entry_time = pos.entry_time,
                                             });
      }
    }

    // Update market countdown every 10 cycles
    if (cycle % 10 == 0) {
      auto mins = 135 - (cycle / 10);
      monitor->set_market_status(true, std::format("Closes in {}m", mins));
    }

    ++cycle;
    std::this_thread::sleep_for(500ms);
  }

  renderer->stop();
  std::println("\nTUI demo finished");

  return 0;
}
