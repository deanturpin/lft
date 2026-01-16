#!/usr/bin/env bash
set -euo pipefail

# Post-match analysis for LFT trading session
# Shows: start/end/low/peak balances, trade statistics, strategy performance

source .env

echo "📊 POST-MATCH ANALYSIS - $(TZ=America/New_York date '+%Y-%m-%d')"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# Get portfolio history for today (1-day, 15-minute bars)
echo "💰 SESSION BALANCE SUMMARY"
echo "───────────────────────────────────────────────────────────────"

portfolio_data=$(curl -s "https://paper-api.alpaca.markets/v2/account/portfolio/history?period=1D&timeframe=15Min" \
  -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
  -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}")

# Extract balance data using jq
echo "$portfolio_data" | jq -r '
  .equity as $equity |
  .timestamp as $ts |

  # Calculate stats
  ($equity | min) as $low |
  ($equity | max) as $peak |
  $equity[0] as $start |
  $equity[-1] as $end |

  # Find indices
  ($equity | to_entries | min_by(.value) | .key) as $low_idx |
  ($equity | to_entries | max_by(.value) | .key) as $peak_idx |

  # Format timestamps
  ($ts[$low_idx] | strftime("%H:%M ET")) as $low_time |
  ($ts[$peak_idx] | strftime("%H:%M ET")) as $peak_time |

  # Calculate changes
  ($end - $start) as $session_pnl |
  (($session_pnl / $start) * 100) as $session_pnl_pct |
  ($peak - $start) as $peak_gain |
  (($peak_gain / $start) * 100) as $peak_gain_pct |
  ($end - $low) as $recovery |

  "Starting Balance: $\($start | tonumber | . * 100 | round / 100)
Current Balance:  $\($end | tonumber | . * 100 | round / 100)
Session P&L:      $\($session_pnl | tonumber | . * 100 | round / 100) (\($session_pnl_pct | tonumber | . * 100 | round / 100)%)

Peak:    $\($peak | tonumber | . * 100 | round / 100) at \($peak_time) (+$\($peak_gain | tonumber | . * 100 | round / 100), +\($peak_gain_pct | tonumber | . * 100 | round / 100)%)
Low:     $\($low | tonumber | . * 100 | round / 100) at \($low_time)
Recovery: $\($recovery | tonumber | . * 100 | round / 100) from low to close"
'

echo ""
echo "───────────────────────────────────────────────────────────────"
echo ""

# Get today's closed orders
echo "📈 TRADE STATISTICS"
echo "───────────────────────────────────────────────────────────────"

TODAY=$(TZ=America/New_York date '+%Y-%m-%d')
orders=$(curl -s "https://paper-api.alpaca.markets/v2/orders?status=closed&after=${TODAY}T00:00:00Z&limit=500" \
  -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
  -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}")

# Count trades
total_orders=$(echo "$orders" | jq 'length')
buy_orders=$(echo "$orders" | jq '[.[] | select(.side == "buy")] | length')
sell_orders=$(echo "$orders" | jq '[.[] | select(.side == "sell")] | length')

echo "Total Orders:  $total_orders"
echo "Buy Orders:    $buy_orders"
echo "Sell Orders:   $sell_orders"
echo "Round Trips:   $((sell_orders))"
echo ""

# Calculate P&L by pairing buys and sells
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "💡 For detailed trade analysis:"
echo "   ./scripts/fetch_orders.sh | python3 analyze_trades.py"
echo ""
