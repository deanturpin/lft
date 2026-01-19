// Phase 4: EOD Liquidation
// Closes all positions at end of trading day

#include "lft.h"
#include <print>

namespace lft {

void liquidate_all(AlpacaClient &client) {
  const auto positions = client.get_positions();

  if (positions.empty()) {
    std::println("  No positions to liquidate");
    return;
  }

  for (const auto &pos : positions) {
    std::println("  Liquidating {} ({} shares)", pos.symbol, pos.qty);
    client.close_position(pos.symbol);
  }
}

} // namespace lft
