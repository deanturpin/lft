#!/usr/bin/env python3

import os
import requests
import json
from collections import defaultdict
from datetime import datetime

# Load API credentials
API_KEY = os.getenv('ALPACA_API_KEY')
API_SECRET = os.getenv('ALPACA_API_SECRET')

# Fetch orders
url = "https://paper-api.alpaca.markets/v2/orders"
params = {
    "status": "all",
    "limit": 500,
    "after": "2026-01-13T00:00:00Z",
    "until": "2026-01-30T00:00:00Z"
}
headers = {
    "APCA-API-KEY-ID": API_KEY,
    "APCA-API-SECRET-KEY": API_SECRET
}

response = requests.get(url, params=params, headers=headers)
orders = response.json()

# Separate buys and sells
buys = []
sells = []

for order in orders:
    if order.get('filled_at') is None:
        continue

    if order['side'] == 'buy':
        # Extract strategy from client_order_id
        client_id = order.get('client_order_id', '')
        if client_id and '_' in client_id:
            strategy = client_id.split('_')[1]
        else:
            strategy = 'UNKNOWN'

        buys.append({
            'symbol': order['symbol'],
            'strategy': strategy,
            'price': float(order.get('filled_avg_price', 0)),
            'qty': float(order.get('filled_qty', 0)),
            'time': order['filled_at'],
            'notional': float(order.get('filled_avg_price', 0)) * float(order.get('filled_qty', 0))
        })
    elif order['side'] == 'sell':
        sells.append({
            'symbol': order['symbol'],
            'price': float(order.get('filled_avg_price', 0)),
            'qty': float(order.get('filled_qty', 0)),
            'time': order['filled_at'],
            'notional': float(order.get('filled_avg_price', 0)) * float(order.get('filled_qty', 0))
        })

# Match buys with sells (simple FIFO matching)
strategy_stats = defaultdict(lambda: {'wins': 0, 'losses': 0, 'total_pnl': 0.0, 'trades': []})

# Sort by time
buys.sort(key=lambda x: x['time'])
sells.sort(key=lambda x: x['time'])

# Match sells to buys
for sell in sells:
    # Find matching buy (same symbol, earlier time)
    for i, buy in enumerate(buys):
        if buy['symbol'] == sell['symbol'] and buy['time'] < sell['time']:
            # Calculate P&L
            pnl = sell['notional'] - buy['notional']
            pnl_pct = (pnl / buy['notional']) * 100

            strategy = buy['strategy']
            strategy_stats[strategy]['trades'].append({
                'symbol': buy['symbol'],
                'buy_price': buy['price'],
                'sell_price': sell['price'],
                'pnl': pnl,
                'pnl_pct': pnl_pct,
                'date': buy['time'][:10]
            })

            if pnl > 0:
                strategy_stats[strategy]['wins'] += 1
            else:
                strategy_stats[strategy]['losses'] += 1

            strategy_stats[strategy]['total_pnl'] += pnl

            # Remove matched buy
            buys.pop(i)
            break

# Print results
print("\n=== Win Rate by Strategy (Jan 13-29, 2026) ===\n")
print(f"{'Strategy':<20} {'Wins':<8} {'Losses':<8} {'Total':<8} {'Win%':<8} {'Total P&L':<12}")
print("-" * 80)

for strategy in sorted(strategy_stats.keys()):
    stats = strategy_stats[strategy]
    total = stats['wins'] + stats['losses']
    win_rate = (stats['wins'] / total * 100) if total > 0 else 0
    print(f"{strategy:<20} {stats['wins']:<8} {stats['losses']:<8} {total:<8} {win_rate:<8.1f} ${stats['total_pnl']:<11.2f}")

print("\n=== Recent Trades by Strategy ===\n")

# Show last 10 trades for each strategy
for strategy in sorted(strategy_stats.keys()):
    print(f"\n{strategy.upper()} (last 10 trades):")
    print(f"{'Date':<12} {'Symbol':<8} {'Buy':<10} {'Sell':<10} {'P&L%':<8} {'P&L $':<10}")
    print("-" * 70)

    recent = strategy_stats[strategy]['trades'][-10:]
    for trade in recent:
        pnl_emoji = "🟢" if trade['pnl'] > 0 else "🔴"
        print(f"{trade['date']:<12} {trade['symbol']:<8} ${trade['buy_price']:<9.2f} ${trade['sell_price']:<9.2f} {trade['pnl_pct']:>6.2f}%  {pnl_emoji} ${trade['pnl']:>8.2f}")
