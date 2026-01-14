// Terminal UI for LFT trading system
#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace ftxui {
class ScreenInteractive;
}

namespace tui {

// Position information for portfolio view
struct position_data {
  std::string symbol;
  std::string strategy;
  double entry_price;
  double current_price;
  double quantity;
  double market_value;
  double unrealised_pnl;
  double unrealised_pct;
  double take_profit_pct;
  double stop_loss_pct;
  double trailing_stop_pct;
  std::chrono::system_clock::time_point entry_time;

  std::string to_string() const;
};

// Market quote with signal information
struct market_quote {
  std::string symbol;
  double price;
  double change_pct;
  std::string last_signal;      // "BUY", "BLOCKED", ""
  std::string signal_strategy;  // "momentum", "mean_reversion"
  bool has_position;

  std::string to_string() const;
};

// Strategy performance statistics
struct strategy_stats {
  std::string name;
  size_t signals_generated;
  size_t signals_executed;
  size_t positions_open;
  size_t positions_closed;
  size_t wins;
  size_t losses;
  double total_pnl;
  double win_rate_pct;

  std::string to_string() const;
};

// Order entry for order book view
struct order_entry {
  size_t number;
  std::chrono::system_clock::time_point timestamp;
  std::string symbol;
  std::string side;      // "buy", "sell"
  std::string status;    // "filled", "pending", "rejected"
  double quantity;
  double fill_price;
  std::string client_order_id;

  std::string to_string() const;
};

// Account information
struct account_info {
  double balance;
  double buying_power;
  size_t positions_count;
};

// Market status (open/closed with countdown)
struct market_status {
  bool is_open;
  std::string countdown;  // "Closes in 2h 15m" or "Opens in 14h 3m"
};

// Thread-safe data store for TUI
class trading_monitor {
public:
  // Position management
  void update_position(std::string_view symbol, const position_data &data);
  void remove_position(std::string_view symbol);
  std::vector<position_data> get_positions() const;

  // Market data
  void update_quote(std::string_view symbol, const market_quote &quote);
  std::vector<market_quote> get_quotes() const;

  // Strategy performance
  void update_strategy_stats(std::string_view strategy,
                             const strategy_stats &stats);
  std::vector<strategy_stats> get_strategies() const;

  // Order history
  void add_order(const order_entry &order);
  std::vector<order_entry> get_recent_orders(size_t count) const;

  // Account status
  void update_account(double balance, double buying_power,
                      size_t positions_count);
  account_info get_account() const;

  // Market status
  void set_market_status(bool is_open, std::string_view countdown);
  market_status get_market_status() const;

  // System stats
  void increment_cycle_count();
  size_t get_cycle_count() const;

  // Get total unrealised P&L across all positions
  double get_total_unrealised_pnl() const;
  double get_total_unrealised_pct() const;

private:
  mutable std::mutex mutex_;
  std::map<std::string, position_data> positions_;
  std::map<std::string, market_quote> quotes_;
  std::map<std::string, strategy_stats> strategies_;
  std::deque<order_entry> orders_;
  account_info account_{};
  market_status market_{};
  std::atomic<size_t> cycle_count_{0uz};
  static constexpr auto MAX_ORDERS = 100uz; // Ringbuffer size for orders
};

// Main TUI renderer - manages screen updates
class tui_renderer {
public:
  explicit tui_renderer(std::shared_ptr<trading_monitor> store);
  ~tui_renderer();

  // Start rendering loop (runs in separate thread)
  void start();

  // Stop rendering and restore terminal
  void stop();

  // Check if renderer is running
  bool is_running() const;

  // Set callback for when user quits (presses q/Esc)
  void set_quit_callback(std::function<void()> callback);

private:
  std::shared_ptr<trading_monitor> store_;
  std::optional<std::reference_wrapper<ftxui::ScreenInteractive>>
      screen_; // Reference to FTXUI screen for cleanup
  std::atomic<bool> screen_active_{
      false}; // Thread-safe flag for screen validity

  // View mode enumeration
  enum class view_mode { portfolio, market, strategy, orders };
  std::atomic<int> current_view_{0}; // Cast view_mode to int for atomic

  // Braille spinner animation state (thread-safe)
  std::atomic<size_t> spinner_frame_{0uz};
  static constexpr std::array<const char *, 8> SPINNER_FRAMES = {
      "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧"};

  // Pause state
  std::atomic<bool> paused_{false};

  // Callback invoked when user quits
  std::function<void()> quit_callback_;

  void render_loop();
};

// Check if terminal supports interactive TUI
bool supports_interactive();

} // namespace tui
