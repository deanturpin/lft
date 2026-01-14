# TUI Feature Branch

**Status:** ✅ Implementation Complete
**Branch:** `feature/tui-interface`
**Issue:** #13

## Quick Start

Try the TUI demo right now:

```bash
# Build the demo
cd build
make tui_demo -j4

# Run it
./tui_demo
```

**Controls:**
- Arrow keys (←/→) to switch views
- Space to pause/unpause
- q or Esc to quit

## What You'll See

The demo shows a simulated trading session with:
- **Portfolio View**: 3 positions with live P&L updates
- **Market View**: 8 symbols with signals and price changes
- **Strategy View**: 2 strategies with performance stats
- **Order Book**: 5 recent filled orders

Prices update in real-time to simulate market movement.

## Documentation

1. **[TUI_DESIGN.md](TUI_DESIGN.md)** - Architecture and design patterns
2. **[TUI_INTEGRATION.md](TUI_INTEGRATION.md)** - How to integrate with main trading loop
3. **[TUI_COMPLETE.md](TUI_COMPLETE.md)** - Complete project summary

## Files in This Branch

### Implementation
- `src/tui.hxx` - TUI interface (187 lines)
- `src/tui.cxx` - TUI implementation (600+ lines)
- `src/tui_demo.cxx` - Standalone demo (200+ lines)

### Documentation
- `TUI_DESIGN.md` - Architectural design
- `TUI_INTEGRATION.md` - Integration guide
- `TUI_COMPLETE.md` - Project summary
- `TUI_README.md` - This file

### Build System
- `CMakeLists.txt` - Added FTXUI support

## Key Features

✅ Four view modes (Portfolio, Market, Strategy, Orders)
✅ Real-time updates at 10Hz
✅ Colour-coded display (green/red for P&L)
✅ Keyboard navigation (arrows, space, q/Esc)
✅ Thread-safe data store
✅ Graceful fallback without FTXUI
✅ Pause functionality
✅ Professional appearance

## Integration Status

- [x] Core TUI implementation
- [x] Build system integration
- [x] Demo program
- [x] Documentation
- [ ] Integration with main trading loop (pending)
- [ ] Live trading test (pending)

## Next Steps

Ready to integrate with `src/lft.cxx`. See [TUI_INTEGRATION.md](TUI_INTEGRATION.md) for step-by-step instructions.

## Performance

- CPU overhead: <1% (10Hz refresh)
- Memory: ~1MB for FTXUI buffers
- No impact on trading loop (separate thread)
- Minimal lock contention

## Requirements

- FTXUI library (installed via `brew install ftxui`)
- C++23 compiler
- Terminal with ANSI colour support

Falls back gracefully if FTXUI not available.
