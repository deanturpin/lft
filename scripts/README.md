# LFT Utility Scripts

Collection of utility scripts for analyzing trading data and interacting with the Alpaca API.

## fetch_orders.sh

Fetch and display all orders from the Alpaca API.

### Usage

```bash
# Display orders in terminal
./scripts/fetch_orders.sh

# Export to CSV
./scripts/fetch_orders.sh --csv orders.csv
```

### Requirements

- Environment variables: `ALPACA_API_KEY`, `ALPACA_API_SECRET`
- Optional: `ALPACA_BASE_URL` (defaults to paper trading)
- `jq` for pretty formatting (optional, falls back to Python)

### Output Format

Terminal output shows:
- Created timestamp
- Symbol
- Side (buy/sell)
- Quantity
- Notional amount
- Status
- Client order ID (with strategy parameters)

CSV export includes all order fields for further analysis.
