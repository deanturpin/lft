# TUI Implementation Design

**Feature Branch:** `feature/tui-interface`
**GitHub Issue:** #13
**Reference:** [stooge project](https://github.com/deanturpin/stooge) for FTXUI patterns

---

## Architecture Overview

Following the stooge project's proven pattern:

1. **Thread-safe data store** (`trading_monitor` class) - similar to stooge's `traffic_monitor`
2. **Renderer** (`tui_renderer` class) - manages FTXUI screen updates
3. **Main trading loop** - populates data store, runs in background
4. **Render loop** - consumes data store, updates TUI at 10Hz

### Thread Model

```
┌─────────────────────┐     ┌──────────────────────┐
│  Main Trading Loop  │────▶│  trading_monitor     │
│  (existing code)    │     │  (thread-safe data)  │
└─────────────────────┘     └──────────────────────┘
                                      │
                                      │ reads
                                      ▼
                            ┌──────────────────────┐
                            │   tui_renderer       │
                            │   (FTXUI screen)     │
                            └──────────────────────┘
```

---

## Data Model

### `trading_monitor` Class

Thread-safe storage for all TUI data (protected by `std::mutex`):

```cpp
class trading_monitor {
public:
  // Position management
  void update_position(std::string_view symbol, const position_data& data);
  void remove_position(std::string_view symbol);
  std::vector<position_data> get_positions() const;

  // Market data
  void update_price(std::string_view symbol, double price, double change_pct);
  std::vector<market_quote> get_quotes() const;

  // Strategy performance
  void update_strategy_stats(std::string_view strategy, const strategy_stats& stats);
  std::vector<strategy_stats> get_strategies() const;

  // Order history
  void add_order(const order_entry& order);
  std::vector<order_entry> get_recent_orders(size_t count) const;

  // Account status
  void update_account(double balance, double buying_power);
  account_info get_account() const;

  // Market status
  void set_market_status(bool is_open, std::string_view countdown);
  market_status get_market_status() const;

  // System stats
  void increment_cycle_count();
  size_t get_cycle_count() const;

private:
  mutable std::mutex mutex_;
  std::map<std::string, position_data> positions_;
  std::map<std::string, market_quote> quotes_;
  std::map<std::string, strategy_stats> strategies_;
  std::deque<order_entry> orders_;
  account_info account_;
  market_status market_;
  std::atomic<size_t> cycle_count_{0uz};
};
```

### Data Structures

```cpp
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
};

struct market_quote {
  std::string symbol;
  double price;
  double change_pct;
  std::string last_signal;      // "BUY", "BLOCKED", ""
  std::string signal_strategy;  // "momentum", "mean_reversion"
  bool has_position;
};

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
};

struct order_entry {
  size_t number;
  std::chrono::system_clock::time_point timestamp;
  std::string symbol;
  std::string side;      // "buy", "sell"
  std::string status;    // "filled", "pending", "rejected"
  double quantity;
  double fill_price;
  std::string client_order_id;
};

struct account_info {
  double balance;
  double buying_power;
  size_t positions_count;
};

struct market_status {
  bool is_open;
  std::string countdown;  // "Closes in 2h 15m" or "Opens in 14h 3m"
};
```

---

## View Modes

### 1. Portfolio View (Default)

**Layout:**

```
┌─────────────────────────────────────────────────────────────────────────┐
│ LFT v1.0.0 | Balance: $95,432 | Buying Power: $50,000 | Positions: 12  │
│ Market: OPEN (Closes in 2h 15m) | Cycle: 1,234 | ⠋ Live                │
└─────────────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────────────┐
│ PORTFOLIO (12 positions)                       [←/→ to switch views]    │
├─────────────────────────────────────────────────────────────────────────┤
│ Symbol  Strategy       Entry    Current   Qty    Value   P&L    P&L%   │
│ NVDA    mean_reversion $875.50  $895.32   1.14  $1,020  +$23  +2.26%  │
│ AAPL    momentum       $185.20  $184.10   5.40    $994   -$6  -0.59%  │
│ ETH/USD mean_reversion $3,420   $3,398    0.29    $986  -$14  -1.42%  │
│ ...                                                                     │
│                                                                         │
│ Exit Criteria:  TP: 2.0%  SL: -2.0%  Trailing: 1.0%                   │
│                                                                         │
│ Total Unrealised: +$45.23 (+0.23%)                                     │
└─────────────────────────────────────────────────────────────────────────┘
```

**Colour Coding:**
- Green: Positive P&L
- Red: Negative P&L
- Cyan: Symbol names
- Yellow: Exit criteria warnings (close to stop loss)

### 2. Market View

**Layout:**

```
┌─────────────────────────────────────────────────────────────────────────┐
│ MARKET SIGNALS                                 [←/→ to switch views]    │
├─────────────────────────────────────────────────────────────────────────┤
│ Symbol    Price     Change%  Signal    Strategy        Status          │
│ TSLA      $245.32   +2.45%   BUY       momentum        ✓ Executed      │
│ AVAX/USD  $35.21    -1.23%   BLOCKED   mean_reversion  Position open   │
│ SPY       $485.90   +0.15%   ---       ---             Watching        │
│ DOGE/USD  $0.0825   +5.67%   BUY       mean_reversion  ✓ Executed      │
│ ...                                                                     │
└─────────────────────────────────────────────────────────────────────────┘
```

**Colour Coding:**
- Green: BUY signals
- Yellow: BLOCKED signals
- White: No signal
- Cyan: Strategy names

### 3. Strategy Performance View

**Layout:**

```
┌─────────────────────────────────────────────────────────────────────────┐
│ STRATEGY PERFORMANCE                           [←/→ to switch views]    │
├─────────────────────────────────────────────────────────────────────────┤
│ Strategy        Signals  Executed  Open  Closed  Wins  Win%   P&L      │
│ mean_reversion  45       32        8     24      15    62.5%  +$234.50 │
│ momentum        38       28        4     24      18    75.0%  +$512.30 │
│                                                                         │
│ Total           83       60        12    48      33    68.8%  +$746.80 │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4. Order Book View

**Layout:**

```
┌─────────────────────────────────────────────────────────────────────────┐
│ RECENT ORDERS (Last 20)                        [←/→ to switch views]    │
├─────────────────────────────────────────────────────────────────────────┤
│ #    Time     Symbol    Side  Status   Qty    Fill Price  Order ID     │
│ 1234 15:32:15 NVDA      buy   filled   1.14   $875.50     NVDA_mr_... │
│ 1233 15:31:42 ETH/USD   sell  filled   0.29   $3,398.21   ETH_mr_...  │
│ 1232 15:30:18 AAPL      buy   filled   5.40   $185.20     AAPL_mom_.. │
│ ...                                                                     │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Integration with Existing Code

### Minimal Changes to `src/lft.cxx`

1. **Create trading_monitor instance** (global or in main):

```cpp
auto monitor = std::make_shared<tui::trading_monitor>();
```

2. **Update positions when they change**:

```cpp
// After opening position
monitor->update_position(symbol, position_data{
  .symbol = symbol,
  .strategy = strategy,
  .entry_price = entry_price,
  // ... other fields
});

// After closing position
monitor->remove_position(symbol);
```

3. **Update account info each cycle**:

```cpp
monitor->update_account(account_balance, buying_power);
monitor->increment_cycle_count();
```

4. **Launch TUI renderer**:

```cpp
auto renderer = std::make_unique<tui::tui_renderer>(monitor);
renderer->start();  // Runs in separate thread

// Main trading loop continues unchanged
while (trading) {
  // Existing trading logic...
  // Just update monitor with new data
}

renderer->stop();  // Clean shutdown
```

### Fallback Mode

If terminal doesn't support FTXUI (SSH sessions, etc.), fall back to existing text output:

```cpp
if (not tui::supports_interactive()) {
  std::println("TUI not supported, using text mode");
  // Use existing println-based output
}
```

---

## File Structure

```
src/
├── tui.hxx               # TUI interface (trading_monitor + tui_renderer)
├── tui.cxx               # TUI implementation
└── lft.cxx               # Main trading loop (minimal changes)

include/
└── (no changes needed)
```

---

## Dependencies

Add to `CMakeLists.txt`:

```cmake
# Find FTXUI for terminal UI
find_package(ftxui REQUIRED)

target_link_libraries(lft
    # ... existing libraries
    ftxui::screen ftxui::dom ftxui::component
)
```

Install FTXUI on macOS:
```bash
brew install ftxui
```

---

## Implementation Phases

### Phase 1: Infrastructure (Minimal Viable TUI)
- Create `trading_monitor` class with position tracking
- Create `tui_renderer` with basic layout
- Implement Portfolio View only
- Header bar with account stats
- Keyboard controls: q to quit, Space to pause

### Phase 2: Additional Views
- Market View
- Strategy Performance View
- Order Book View
- Arrow key navigation between views

### Phase 3: Polish
- Colour coding
- Spinner animation
- Dynamic column widths
- Pause overlay
- Scrolling support for long lists

---

## Testing Strategy

1. **Unit tests** for `trading_monitor` thread safety
2. **Manual testing** in paper trading mode
3. **Stress testing** with rapid position changes
4. **Terminal compatibility** testing (iTerm2, Terminal.app, tmux)

---

## Success Criteria

- [ ] TUI displays current positions with real-time P&L
- [ ] Multiple view modes work correctly
- [ ] Keyboard controls are responsive
- [ ] No race conditions or data corruption
- [ ] Graceful fallback to text mode if TUI unavailable
- [ ] Minimal performance impact on trading loop (<1ms overhead)

---

## Non-Goals for Initial Implementation

- Mouse clicking (keyboard only)
- Charts/graphs (text-based display only)
- Trade execution from TUI (monitoring only)
- Configuration editing (monitor only, configuration via command line/config files)
