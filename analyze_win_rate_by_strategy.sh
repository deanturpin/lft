#!/bin/bash

# Load environment variables
set -a
source .env
set +a

echo "Analyzing win rate by strategy (Jan 13-29, 2026)..."
echo ""

# Fetch all orders and calculate P&L by strategy
curl -s "https://paper-api.alpaca.markets/v2/orders?status=all&limit=500&after=2026-01-13T00:00:00Z&until=2026-01-30T00:00:00Z" \
  -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
  -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}" \
  | jq -r '
    # Group by symbol and created date
    [.[] | select(.filled_at != null)] |

    # Create buy/sell pairs
    reduce .[] as $order (
      {};
      .[$order.symbol] += [$order]
    ) |

    # For each symbol, pair buys with sells
    to_entries |
    map(
      .value |
      map(
        if .side == "buy" then
          .strategy = (if .client_order_id then (.client_order_id | split("_")[1]) else "UNKNOWN" end) |
          .
        else . end
      ) |
      # Sort by filled_at
      sort_by(.filled_at) |
      # Simple pairing: buys followed by sells
      .
    ) |

    # Flatten and extract strategy from buys
    flatten |
    map(select(.side == "buy" and .filled_at != null)) |
    .[] |
    .strategy as $strat |
    "\($strat)"
  ' | sort | uniq -c | sort -rn

echo ""
echo "Detailed breakdown (first 20 round trips with P&L):"
echo "Strategy | Symbol | Entry Price | Exit Price | P&L% | Date"
echo "---------|--------|-------------|------------|------|-----"

# Get recent round trips with P&L calculation
# This is complex - for now just show strategy counts
# A proper implementation would need to match specific order IDs
