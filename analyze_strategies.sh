#!/bin/bash

# Load environment variables
set -a
source .env
set +a

echo "Fetching all buy orders from Jan 13 - Jan 29, 2026..."
echo ""

# Fetch orders from the last couple weeks
curl -s "https://paper-api.alpaca.markets/v2/orders?status=all&limit=500&after=2026-01-13T00:00:00Z&until=2026-01-30T00:00:00Z" \
  -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
  -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}" \
  | jq -r '
    .[] |
    select(.side == "buy") |
    .client_order_id as $oid |
    if $oid then
      ($oid | split("_")[1]) as $strategy |
      ($oid | split("|")[0] | split("_")[0]) as $symbol |
      "\(.filled_at[:10])\t\($symbol)\t\($strategy)\t\(.filled_avg_price)\t\(.filled_qty)"
    else
      "\(.filled_at[:10])\t\(.symbol)\tUNKNOWN\t\(.filled_avg_price)\t\(.filled_qty)"
    end
  ' | sort

echo ""
echo "Strategy breakdown:"
echo ""

curl -s "https://paper-api.alpaca.markets/v2/orders?status=all&limit=500&after=2026-01-13T00:00:00Z&until=2026-01-30T00:00:00Z" \
  -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
  -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}" \
  | jq -r '
    [.[] | select(.side == "buy") | .client_order_id] |
    map(if . then (split("_")[1]) else "UNKNOWN" end) |
    group_by(.) |
    map({strategy: .[0], count: length}) |
    .[] |
    "\(.strategy)\t\(.count)"
  ' | sort -k2 -rn
