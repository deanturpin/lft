#!/bin/bash
# Fetch today's closed orders from Alpaca API

source .env

# Get today's date in ISO format
TODAY=$(date -u +"%Y-%m-%d")

echo "Fetching closed orders for $TODAY..."
echo ""

# Fetch orders with status=closed from today
curl -s "https://paper-api.alpaca.markets/v2/orders?status=closed&after=${TODAY}T00:00:00Z&limit=500" \
  -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
  -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}" | \
  python3 -c "
import sys, json
from datetime import datetime

orders = json.load(sys.stdin)

print(f'Found {len(orders)} closed orders today\n')
print(f'{\"Symbol\":<8} {\"Side\":<6} {\"Qty\":<8} {\"Price\":<10} {\"Notional\":<12} {\"Time\":<20} {\"Status\":<10}')
print('-' * 90)

total_buy = 0
total_sell = 0

for order in orders:
    symbol = order['symbol']
    side = order['side']
    qty = order.get('filled_qty', order.get('qty', '0'))
    filled_price = order.get('filled_avg_price', '0')
    notional = float(order.get('filled_avg_price', 0)) * float(order.get('filled_qty', 0)) if order.get('filled_avg_price') else 0
    filled_at = order.get('filled_at', order.get('created_at', ''))[:19]
    status = order['status']

    print(f'{symbol:<8} {side:<6} {qty:<8} {filled_price:<10} {notional:<12.2f} {filled_at:<20} {status:<10}')

    if side == 'buy':
        total_buy += notional
    elif side == 'sell':
        total_sell += notional

print('-' * 90)
print(f'Total bought:  ${total_buy:,.2f}')
print(f'Total sold:    ${total_sell:,.2f}')
print(f'Net P&L:       ${total_sell - total_buy:,.2f}')
"
