#!/usr/bin/env python3
import os
import requests
from datetime import datetime, timedelta

# Get API keys from environment
api_key = os.environ.get('ALPACA_API_KEY')
api_secret = os.environ.get('ALPACA_API_SECRET')

if not api_key or not api_secret:
    print("Error: ALPACA_API_KEY and ALPACA_API_SECRET must be set")
    exit(1)

base_url = "https://paper-api.alpaca.markets"
headers = {
    "APCA-API-KEY-ID": api_key,
    "APCA-API-SECRET-KEY": api_secret
}

# Get bars for today (1-minute resolution)
symbols = ["AAPL", "NVDA", "TSLA", "AMZN", "GOOGL", "MSFT", "META", "BABA", "AMD", "TLT"]

# Today's date range (market opens 9:30 AM ET, currently ~11:30 AM ET based on your logs)
now = datetime.utcnow()
start = (now - timedelta(hours=3)).strftime("%Y-%m-%dT%H:%M:%SZ")  # Last 3 hours
end = now.strftime("%Y-%m-%dT%H:%M:%SZ")

print(f"Fetching 1-minute bars from {start} to {end}\n")
print(f"{'Symbol':<8} {'Bars':<8} {'Open':<10} {'Close':<10} {'Change %':<10} {'High-Low':<10} {'Avg Vol':<12}")
print("-" * 80)

for symbol in symbols:
    url = f"{base_url}/v2/stocks/{symbol}/bars"
    params = {
        "timeframe": "1Min",
        "start": start,
        "end": end,
        "limit": 10000
    }
    
    response = requests.get(url, headers=headers, params=params)
    
    if response.status_code == 200:
        data = response.json()
        bars = data.get("bars", [])
        
        if bars:
            first_bar = bars[0]
            last_bar = bars[-1]
            
            open_price = first_bar["o"]
            close_price = last_bar["c"]
            change_pct = ((close_price - open_price) / open_price) * 100
            
            # Calculate average high-low range
            ranges = [(b["h"] - b["l"]) for b in bars]
            avg_range = sum(ranges) / len(ranges)
            range_pct = (avg_range / close_price) * 100
            
            # Calculate average volume
            volumes = [b["v"] for b in bars]
            avg_vol = sum(volumes) / len(volumes)
            
            print(f"{symbol:<8} {len(bars):<8} ${open_price:<9.2f} ${close_price:<9.2f} {change_pct:>+9.2f}% {range_pct:>9.3f}% {avg_vol:>11,.0f}")
        else:
            print(f"{symbol:<8} No data")
    else:
        print(f"{symbol:<8} Error: {response.status_code}")

print("\nNote: High-Low % shows average 1-minute bar range as % of price (volatility indicator)")
