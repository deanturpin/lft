#!/bin/bash

# Load environment variables
set -a
source .env
set +a

# Fetch orders from yesterday (Jan 29, 2026)
echo "Fetching orders for 2026-01-29..."

curl -s "https://paper-api.alpaca.markets/v2/orders?status=all&limit=100&after=2026-01-29T00:00:00Z&until=2026-01-30T00:00:00Z" \
  -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
  -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}" \
  | jq -r '.[] | select(.side == "buy") | "\(.symbol)\t\(.client_order_id)\t\(.filled_at)"' \
  | sort -k3
