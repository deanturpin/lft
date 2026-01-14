// Terminal UI implementation
#include "tui.hxx"
#include <algorithm>
#include <format>
#include <iomanip>
#include <numeric>
#include <print>
#include <sstream>
#include <unistd.h>

#ifdef HAVE_FTXUI
#include <ftxui/component/component.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#endif

namespace tui {

// Format currency with 2 decimal places
std::string format_currency(double value) {
  return std::format("${:.2f}", value);
}

// Format percentage with 2 decimal places and sign
std::string format_percentage(double value) {
  return std::format("{:+.2f}%", value);
}

// Format timestamp as HH:MM:SS
std::string format_time(const std::chrono::system_clock::time_point &tp) {
  auto time_t = std::chrono::system_clock::to_time_t(tp);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
  return ss.str();
}

// position_data::to_string implementation
std::string position_data::to_string() const {
  return std::format("{:8} {:14} {:8.2f} {:8.2f} {:6.2f} {:9} {:7} {:7}",
                     symbol.substr(0, 8), strategy.substr(0, 14), entry_price,
                     current_price, quantity, format_currency(market_value),
                     format_currency(unrealised_pnl),
                     format_percentage(unrealised_pct));
}

// market_quote::to_string implementation
std::string market_quote::to_string() const {
  auto signal_str = last_signal.empty() ? "---" : last_signal;
  auto strategy_str = signal_strategy.empty() ? "---" : signal_strategy;
  auto status = has_position ? "Position open" : (last_signal == "BUY" ? "✓ Executed" : "Watching");
  return std::format("{:10} {:9.2f} {:8} {:8} {:15} {}", symbol.substr(0, 10),
                     price, format_percentage(change_pct), signal_str,
                     strategy_str.substr(0, 15), status);
}

// strategy_stats::to_string implementation
std::string strategy_stats::to_string() const {
  return std::format("{:15} {:7} {:8} {:5} {:6} {:5} {:6.1f}% {}",
                     name.substr(0, 15), signals_generated, signals_executed,
                     positions_open, positions_closed, wins, win_rate_pct,
                     format_currency(total_pnl));
}

// order_entry::to_string implementation
std::string order_entry::to_string() const {
  return std::format("{:4} {} {:10} {:4} {:8} {:6.2f} {:10.2f} {}",
                     number, format_time(timestamp), symbol.substr(0, 10),
                     side.substr(0, 4), status.substr(0, 8), quantity,
                     fill_price, client_order_id.substr(0, 15));
}

// trading_monitor implementation
void trading_monitor::update_position(std::string_view symbol,
                                      const position_data &data) {
  auto lock = std::scoped_lock{mutex_};
  positions_[std::string{symbol}] = data;
}

void trading_monitor::remove_position(std::string_view symbol) {
  auto lock = std::scoped_lock{mutex_};
  positions_.erase(std::string{symbol});
}

std::vector<position_data> trading_monitor::get_positions() const {
  auto lock = std::scoped_lock{mutex_};
  auto result = std::vector<position_data>{};
  result.reserve(positions_.size());

  for (const auto &[key, pos] : positions_)
    result.push_back(pos);

  // Sort by unrealised P&L (winners first)
  std::ranges::sort(result, [](const auto &a, const auto &b) {
    return a.unrealised_pnl > b.unrealised_pnl;
  });

  return result;
}

void trading_monitor::update_quote(std::string_view symbol,
                                   const market_quote &quote) {
  auto lock = std::scoped_lock{mutex_};
  quotes_[std::string{symbol}] = quote;
}

std::vector<market_quote> trading_monitor::get_quotes() const {
  auto lock = std::scoped_lock{mutex_};
  auto result = std::vector<market_quote>{};
  result.reserve(quotes_.size());

  for (const auto &[key, quote] : quotes_)
    result.push_back(quote);

  // Sort: signals first, then by change percentage
  std::ranges::sort(result, [](const auto &a, const auto &b) {
    auto a_has_signal = not a.last_signal.empty();
    auto b_has_signal = not b.last_signal.empty();
    if (a_has_signal != b_has_signal)
      return a_has_signal;
    return std::abs(a.change_pct) > std::abs(b.change_pct);
  });

  return result;
}

void trading_monitor::update_strategy_stats(std::string_view strategy,
                                            const strategy_stats &stats) {
  auto lock = std::scoped_lock{mutex_};
  strategies_[std::string{strategy}] = stats;
}

std::vector<strategy_stats> trading_monitor::get_strategies() const {
  auto lock = std::scoped_lock{mutex_};
  auto result = std::vector<strategy_stats>{};
  result.reserve(strategies_.size());

  for (const auto &[key, stats] : strategies_)
    result.push_back(stats);

  // Sort by total P&L (best first)
  std::ranges::sort(result, [](const auto &a, const auto &b) {
    return a.total_pnl > b.total_pnl;
  });

  return result;
}

void trading_monitor::add_order(const order_entry &order) {
  auto lock = std::scoped_lock{mutex_};
  orders_.push_back(order);
  if (orders_.size() > MAX_ORDERS)
    orders_.pop_front();
}

std::vector<order_entry>
trading_monitor::get_recent_orders(size_t count) const {
  auto lock = std::scoped_lock{mutex_};
  auto result = std::vector<order_entry>{};
  auto start = orders_.size() > count ? orders_.size() - count : 0uz;
  result.reserve(orders_.size() - start);

  for (auto i = start; i < orders_.size(); ++i)
    result.push_back(orders_[i]);

  // Reverse to show most recent first
  std::ranges::reverse(result);
  return result;
}

void trading_monitor::update_account(double balance, double buying_power,
                                     size_t positions_count) {
  auto lock = std::scoped_lock{mutex_};
  account_.balance = balance;
  account_.buying_power = buying_power;
  account_.positions_count = positions_count;
}

account_info trading_monitor::get_account() const {
  auto lock = std::scoped_lock{mutex_};
  return account_;
}

void trading_monitor::set_market_status(bool is_open,
                                        std::string_view countdown) {
  auto lock = std::scoped_lock{mutex_};
  market_.is_open = is_open;
  market_.countdown = countdown;
}

market_status trading_monitor::get_market_status() const {
  auto lock = std::scoped_lock{mutex_};
  return market_;
}

void trading_monitor::increment_cycle_count() { ++cycle_count_; }

size_t trading_monitor::get_cycle_count() const { return cycle_count_.load(); }

double trading_monitor::get_total_unrealised_pnl() const {
  auto lock = std::scoped_lock{mutex_};
  return std::accumulate(
      positions_.begin(), positions_.end(), 0.0,
      [](double sum, const auto &p) { return sum + p.second.unrealised_pnl; });
}

double trading_monitor::get_total_unrealised_pct() const {
  auto lock = std::scoped_lock{mutex_};
  if (positions_.empty())
    return 0.0;

  auto total_value = std::accumulate(positions_.begin(), positions_.end(), 0.0,
                                     [](double sum, const auto &p) {
                                       return sum + p.second.market_value;
                                     });
  auto total_pnl = get_total_unrealised_pnl();

  return total_value > 0.0 ? (total_pnl / total_value) * 100.0 : 0.0;
}

#ifdef HAVE_FTXUI

// tui_renderer implementation
tui_renderer::tui_renderer(std::shared_ptr<trading_monitor> store)
    : store_{std::move(store)} {}

tui_renderer::~tui_renderer() { stop(); }

void tui_renderer::start() {
  if (screen_active_.load())
    return;

  screen_active_.store(true);
  render_loop();
}

void tui_renderer::stop() {
  if (not screen_active_.load())
    return;

  screen_active_.store(false);

  if (screen_.has_value())
    screen_->get().Exit();
}

bool tui_renderer::is_running() const { return screen_active_.load(); }

void tui_renderer::set_quit_callback(std::function<void()> callback) {
  quit_callback_ = std::move(callback);
}

void tui_renderer::render_loop() {
  using namespace ftxui;

  auto screen = ScreenInteractive::Fullscreen();
  screen_ = std::ref(screen);

  // View mode names
  std::vector<std::string> view_names = {"Portfolio", "Market", "Strategy",
                                         "Orders"};

  auto component = Renderer([&] {
    // Advance spinner
    spinner_frame_.store((spinner_frame_.load() + 1) % SPINNER_FRAMES.size());

    // Get current view mode
    auto view = static_cast<view_mode>(current_view_.load());
    auto account = store_->get_account();
    auto market = store_->get_market_status();
    auto cycle = store_->get_cycle_count();

    // Header bar
    auto header = hbox({
        text("LFT v1.0") | bold,
        separator(),
        text(std::format("Balance: {}", format_currency(account.balance))),
        separator(),
        text(std::format("Buying Power: {}",
                         format_currency(account.buying_power))),
        separator(),
        text(std::format("Positions: {}", account.positions_count)),
        separator(),
        text(std::format("Market: {} ({})", market.is_open ? "OPEN" : "CLOSED",
                         market.countdown)),
        separator(),
        text(std::format("Cycle: {}", cycle)),
        separator(),
        text(SPINNER_FRAMES[spinner_frame_.load()]) | color(Color::Cyan),
        text(" Live"),
    });

    // View selector
    auto view_selector = hbox({
        text(view_names[current_view_.load()]) | bold | color(Color::Cyan),
        text(" [←/→ to switch views]") | dim,
    });

    // Content based on view mode
    Elements content_elements;

    if (view == view_mode::portfolio) {
      // Portfolio view
      auto positions = store_->get_positions();
      auto total_pnl = store_->get_total_unrealised_pnl();
      auto total_pct = store_->get_total_unrealised_pct();

      content_elements.push_back(
          text(std::format("PORTFOLIO ({} positions)", positions.size())) |
          bold);
      content_elements.push_back(separator());
      content_elements.push_back(text("Symbol  Strategy       Entry    "
                                      "Current   Qty    Value     P&L     "
                                      "P&L%") |
                                 bold);

      for (const auto &pos : positions) {
        auto color_choice =
            pos.unrealised_pnl >= 0 ? Color::Green : Color::Red;
        content_elements.push_back(text(pos.to_string()) | color(color_choice));
      }

      if (positions.empty())
        content_elements.push_back(text("No open positions") | dim | center);

      content_elements.push_back(separator());
      content_elements.push_back(
          text(std::format("Total Unrealised: {} ({})",
                           format_currency(total_pnl),
                           format_percentage(total_pct))) |
          bold | color(total_pnl >= 0 ? Color::Green : Color::Red));

    } else if (view == view_mode::market) {
      // Market view
      auto quotes = store_->get_quotes();

      content_elements.push_back(text("MARKET SIGNALS") | bold);
      content_elements.push_back(separator());
      content_elements.push_back(
          text("Symbol     Price     Change%  Signal   Strategy        "
               "Status") |
          bold);

      for (const auto &quote : quotes) {
        auto color_choice = quote.last_signal == "BUY"     ? Color::Green
                            : quote.last_signal == "BLOCKED" ? Color::Yellow
                                                             : Color::White;
        content_elements.push_back(text(quote.to_string()) |
                                   color(color_choice));
      }

      if (quotes.empty())
        content_elements.push_back(text("No market data") | dim | center);

    } else if (view == view_mode::strategy) {
      // Strategy performance view
      auto strategies = store_->get_strategies();

      content_elements.push_back(text("STRATEGY PERFORMANCE") | bold);
      content_elements.push_back(separator());
      content_elements.push_back(
          text("Strategy        Signals Executed Open  Closed Wins  Win%   "
               "P&L") |
          bold);

      for (const auto &stats : strategies) {
        auto color_choice = stats.total_pnl >= 0 ? Color::Green : Color::Red;
        content_elements.push_back(text(stats.to_string()) |
                                   color(color_choice));
      }

      if (strategies.empty())
        content_elements.push_back(text("No strategy data") | dim | center);

      // Calculate totals
      auto total_signals =
          std::accumulate(strategies.begin(), strategies.end(), 0uz,
                          [](auto sum, const auto &s) {
                            return sum + s.signals_generated;
                          });
      auto total_executed =
          std::accumulate(strategies.begin(), strategies.end(), 0uz,
                          [](auto sum, const auto &s) {
                            return sum + s.signals_executed;
                          });
      auto total_open =
          std::accumulate(strategies.begin(), strategies.end(), 0uz,
                          [](auto sum, const auto &s) {
                            return sum + s.positions_open;
                          });
      auto total_closed =
          std::accumulate(strategies.begin(), strategies.end(), 0uz,
                          [](auto sum, const auto &s) {
                            return sum + s.positions_closed;
                          });
      auto total_wins =
          std::accumulate(strategies.begin(), strategies.end(), 0uz,
                          [](auto sum, const auto &s) { return sum + s.wins; });
      auto total_pnl =
          std::accumulate(strategies.begin(), strategies.end(), 0.0,
                          [](auto sum, const auto &s) {
                            return sum + s.total_pnl;
                          });
      auto win_pct =
          total_closed > 0 ? (static_cast<double>(total_wins) / total_closed) *
                                 100.0
                           : 0.0;

      content_elements.push_back(separator());
      content_elements.push_back(
          text(std::format("Total           {:7} {:8} {:5} {:6} {:5} {:6.1f}% "
                           "{}",
                           total_signals, total_executed, total_open,
                           total_closed, total_wins, win_pct,
                           format_currency(total_pnl))) |
          bold | color(total_pnl >= 0 ? Color::Green : Color::Red));

    } else if (view == view_mode::orders) {
      // Order book view
      auto orders = store_->get_recent_orders(20);

      content_elements.push_back(text("RECENT ORDERS (Last 20)") | bold);
      content_elements.push_back(separator());
      content_elements.push_back(
          text("#    Time     Symbol     Side Status   Qty    Fill Price  "
               "Order ID") |
          bold);

      for (const auto &order : orders) {
        auto color_choice = order.status == "filled"   ? Color::Green
                            : order.status == "pending" ? Color::Yellow
                                                         : Color::Red;
        content_elements.push_back(text(order.to_string()) |
                                   color(color_choice));
      }

      if (orders.empty())
        content_elements.push_back(text("No recent orders") | dim | center);
    }

    // Combine all elements
    auto content = vbox(content_elements);

    // Add pause overlay if paused
    if (paused_.load()) {
      content = dbox({
          content,
          vbox({
              filler(),
              hbox({
                  filler(),
                  text("  PAUSED  ") | bold | color(Color::Black) |
                      bgcolor(Color::Yellow),
                  filler(),
              }),
              filler(),
          }),
      });
    }

    return vbox({
               header | border,
               view_selector,
               separator(),
               content | flex | border,
           }) |
           flex;
  });

  // Keyboard event handler
  component |= CatchEvent([&](Event event) {
    if (event == Event::Character('q') || event == Event::Escape) {
      screen.Exit();
      if (quit_callback_)
        quit_callback_();
      return true;
    }
    if (event == Event::Character(' ')) {
      paused_.store(not paused_.load());
      return true;
    }
    if (event == Event::ArrowLeft) {
      auto new_view = (current_view_.load() + 3) % 4; // Move left (wrap)
      current_view_.store(new_view);
      return true;
    }
    if (event == Event::ArrowRight) {
      auto new_view = (current_view_.load() + 1) % 4; // Move right
      current_view_.store(new_view);
      return true;
    }
    return false;
  });

  // Run with periodic refresh (10Hz)
  auto loop = Loop(&screen, component);
  while (screen_active_.load() and not loop.HasQuitted()) {
    loop.RunOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  screen_.reset();
}

bool supports_interactive() {
  // Check if we're in a terminal that supports interactive mode
  return isatty(STDIN_FILENO) and isatty(STDOUT_FILENO);
}

#else

// Stub implementations when FTXUI is not available
tui_renderer::tui_renderer(std::shared_ptr<trading_monitor>) {
  std::println("Warning: TUI not available (FTXUI not found)");
}

tui_renderer::~tui_renderer() = default;

void tui_renderer::start() {
  std::println("TUI not available - running in text-only mode");
}

void tui_renderer::stop() {}

bool tui_renderer::is_running() const { return false; }

void tui_renderer::set_quit_callback(std::function<void()>) {}

void tui_renderer::render_loop() {}

bool supports_interactive() { return false; }

#endif

} // namespace tui
