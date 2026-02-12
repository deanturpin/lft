#!/bin/bash
# Fetch 5-minute bars for all watchlist symbols and save to CSV

set -a && source .env && set +a

# Create output directory
mkdir -p tmp/5min_bars

# Symbols from your watchlist (stocks only, crypto disabled)
SYMBOLS=(
    "SPY" "QQQ" "DIA" "IWM" "RSP" "XLK" "XLF"
    "AAPL" "AMZN" "GOOGL" "META" "MSFT" "NVDA" "TSLA"
    "JPM" "BAC" "GS" "MS"
    "JNJ" "UNH" "PFE" "LLY"
    "PG" "KO" "PEP" "WMT" "COST"
    "CAT" "DE" "HON" "GE"
    "XOM" "CVX" "COP" "SLB"
    "ASML" "SAP" "TSM" "NVO"
    "IEF" "TLT" "VNQ"
    "BABA" "LMND" "CRML"
)

START_DATE=$(date -u -v-30d +%Y-%m-%d)
END_DATE=$(date -u +%Y-%m-%d)

echo "Fetching 5-minute bars from ${START_DATE} to ${END_DATE}"
echo "Output directory: tmp/5min_bars/"
echo ""

for SYMBOL in "${SYMBOLS[@]}"; do
    echo -n "Fetching ${SYMBOL}... "

    RESPONSE=$(curl -s "https://data.alpaca.markets/v2/stocks/${SYMBOL}/bars?timeframe=5Min&start=${START_DATE}&end=${END_DATE}&feed=iex" \
        -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
        -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}")

    # Check if we got bars
    BAR_COUNT=$(echo "$RESPONSE" | jq '.bars | length')

    if [ "$BAR_COUNT" = "null" ] || [ "$BAR_COUNT" = "0" ]; then
        echo "FAILED (no data)"
        continue
    fi

    # Save raw JSON response
    JSON_FILE="tmp/5min_bars/${SYMBOL}_5min.json"
    echo "$RESPONSE" > "$JSON_FILE"

    # Write CSV header and data (chronological order)
    OUTPUT_FILE="tmp/5min_bars/${SYMBOL}_5min.csv"
    echo "timestamp,open,high,low,close,volume" > "$OUTPUT_FILE"
    echo "$RESPONSE" | jq -r '.bars | .[] | [.t, .o, .h, .l, .c, .v] | @csv' >> "$OUTPUT_FILE"

    echo "OK (${BAR_COUNT} bars)"
done

echo ""
echo "Done! Files saved to tmp/5min_bars/"
echo "Total files: $(ls tmp/5min_bars/*.csv 2>/dev/null | wc -l)"
