#!/usr/bin/env python3
import sys
import json
from datetime import datetime

orders = json.load(sys.stdin)

print(f'\nFound {len(orders)} closed orders today\n')
print(f'{"Symbol":<10} {"Side":<6} {"Qty":<10} {"Price":<12} {"Notional":<12} {"Time":<20}')
print('-' * 85)

total_buy = 0
total_sell = 0
pairs = {}

for order in orders:
    symbol = order['symbol']
    side = order['side']
    qty = order.get('filled_qty', order.get('qty', '0'))
    filled_price = order.get('filled_avg_price', '0')
    notional = float(order.get('filled_avg_price', 0)) * float(order.get('filled_qty', 0)) if order.get('filled_avg_price') else 0
    filled_at = order.get('filled_at', order.get('created_at', ''))
    if filled_at:
        filled_at = filled_at[:19]
    else:
        filled_at = 'N/A'

    print(f'{symbol:<10} {side:<6} {qty:<10} ${float(filled_price):<11.2f} ${notional:<11.2f} {filled_at:<20}')

    if side == 'buy':
        total_buy += notional
        if symbol not in pairs:
            pairs[symbol] = {'buys': [], 'sells': []}
        pairs[symbol]['buys'].append(notional)
    elif side == 'sell':
        total_sell += notional
        if symbol not in pairs:
            pairs[symbol] = {'buys': [], 'sells': []}
        pairs[symbol]['sells'].append(notional)

print('-' * 85)
print(f'\nTotal bought:  ${total_buy:,.2f}')
print(f'Total sold:    ${total_sell:,.2f}')
print(f'Net P&L:       ${total_sell - total_buy:,.2f}\n')

print('=' * 85)
print('PER-SYMBOL BREAKDOWN')
print('=' * 85)

total_pnl = 0
for symbol, trades in sorted(pairs.items()):
    buy_sum = sum(trades['buys'])
    sell_sum = sum(trades['sells'])
    pnl = sell_sum - buy_sum
    total_pnl += pnl

    if len(trades['buys']) > 0 and len(trades['sells']) > 0:
        print(f'{symbol:<10} Bought: ${buy_sum:>10.2f}  Sold: ${sell_sum:>10.2f}  P&L: ${pnl:>8.2f}')
    elif len(trades['buys']) > 0:
        print(f'{symbol:<10} Bought: ${buy_sum:>10.2f}  (still open)')
    elif len(trades['sells']) > 0:
        print(f'{symbol:<10} Sold: ${sell_sum:>10.2f}  (close only)')

print('=' * 85)
print(f'Total realized P&L: ${total_pnl:,.2f}')
