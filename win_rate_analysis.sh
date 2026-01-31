#!/bin/bash

set -a
source .env
set +a

echo "=== Fetching order data ==="

# Fetch all orders and save to temp file
curl -s "https://paper-api.alpaca.markets/v2/orders?status=all&limit=500&after=2026-01-13T00:00:00Z&until=2026-01-30T00:00:00Z" \
  -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
  -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}" \
  > /tmp/orders.json

echo ""
echo "=== Strategy Performance Summary ==="
echo ""

# Extract strategy from each buy order and count total
echo "Total trades by strategy:"
cat /tmp/orders.json | jq -r '
  [.[] | select(.side == "buy" and .filled_at != null)] |
  map(
    if .client_order_id then
      (.client_order_id | split("_")[1])
    else
      "UNKNOWN"
    end
  ) |
  group_by(.) |
  map({strategy: .[0], count: length}) |
  sort_by(-.count) |
  .[] |
  "\(.strategy)\t\(.count)"
'

echo ""
echo "=== Sample Round Trips (estimating win rate) ==="
echo ""
echo "Recent mean_reversion trades:"
cat /tmp/orders.json | jq -r '
  [.[] | select(.side == "buy" and .filled_at != null and (.client_order_id // "" | contains("mean_reversion")))] |
  sort_by(.filled_at) |
  reverse |
  .[0:10] |
  .[] |
  "\(.filled_at[:10])\t\(.symbol)\t$\(.filled_avg_price)\t(\(.client_order_id // "no_id"))"
'

echo ""
echo "Recent ma (MA crossover) trades:"
cat /tmp/orders.json | jq -r '
  [.[] | select(.side == "buy" and .filled_at != null and (.client_order_id // "" | contains("_ma_")))] |
  sort_by(.filled_at) |
  reverse |
  .[0:10] |
  .[] |
  "\(.filled_at[:10])\t\(.symbol)\t$\(.filled_avg_price)"
'

echo ""
echo "Recent relative (strength) trades:"
cat /tmp/orders.json | jq -r '
  [.[] | select(.side == "buy" and .filled_at != null and (.client_order_id // "" | contains("relative")))] |
  sort_by(.filled_at) |
  reverse |
  .[0:10] |
  .[] |
  "\(.filled_at[:10])\t\(.symbol)\t$\(.filled_avg_price)"
'

rm /tmp/orders.json
