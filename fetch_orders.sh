#!/bin/bash

# Load environment variables
set -a
source .env
set +a

# Use provided date or default to today
DATE=${1:-$(date +%Y-%m-%d)}
NEXT_DATE=$(date -j -v+1d -f "%Y-%m-%d" "$DATE" +%Y-%m-%d 2>/dev/null || date -d "$DATE + 1 day" +%Y-%m-%d)

echo "Fetching orders for $DATE..."

curl -s "https://paper-api.alpaca.markets/v2/orders?status=all&limit=100&after=${DATE}T00:00:00Z&until=${NEXT_DATE}T00:00:00Z" \
  -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
  -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}" \
  | jq -r '.[] | select(.side == "buy") | "\(.symbol)\t\(.client_order_id)\t\(.filled_at)"' \
  | sort -k3
