#!/usr/bin/env bash

set -euo pipefail

# Check environment variables
if [[ -z "${ALPACA_API_KEY:-}" ]] || [[ -z "${ALPACA_API_SECRET:-}" ]]; then
    echo "❌ ALPACA_API_KEY and ALPACA_API_SECRET must be set" >&2
    exit 1
fi

# Determine base URL (paper trading by default)
BASE_URL="${ALPACA_BASE_URL:-https://paper-api.alpaca.markets}"

echo "🔄 Fetching all orders from Alpaca API..."
echo ""

# Calculate date 7 days ago for filtering
AFTER_DATE=$(date -u -v-7d '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || date -u -d '7 days ago' '+%Y-%m-%dT%H:%M:%SZ')

# Fetch all orders (limit=500 max, filter by last 7 days)
response=$(curl -s -X GET "${BASE_URL}/v2/orders?status=all&limit=500&after=${AFTER_DATE}" \
    -H "APCA-API-KEY-ID: ${ALPACA_API_KEY}" \
    -H "APCA-API-SECRET-KEY: ${ALPACA_API_SECRET}")

# Check if jq is available
if ! command -v jq &> /dev/null; then
    echo "⚠️  jq not installed, showing raw JSON"
    echo "$response" | python3 -m json.tool
    exit 0
fi

# Count total orders
total=$(echo "$response" | jq 'length')
echo "📊 Total orders: $total"
echo ""

# Export to CSV if requested
if [[ "${1:-}" == "--csv" ]]; then
    output_file="${2:-lft_all_orders.csv}"
    echo "💾 Exporting to CSV: $output_file"

    echo "created_at,symbol,side,qty,notional,status,client_order_id,order_id" > "$output_file"

    echo "$response" | jq -r '.[] |
        [.created_at, .symbol, .side, .qty, .notional, .status, .client_order_id, .id] |
        @csv' >> "$output_file"

    echo "✅ Exported $total orders to $output_file"
    exit 0
fi

# Pretty print to console
echo "$(printf '%.0s-' {1..150})"
printf "%-20s %-8s %-6s %-8s %-10s %-12s %-50s\n" \
    "CREATED" "SYMBOL" "SIDE" "QTY" "NOTIONAL" "STATUS" "CLIENT_ORDER_ID"
echo "$(printf '%.0s-' {1..150})"

echo "$response" | jq -r '.[] |
    [
        (.created_at[:19]),
        .symbol,
        .side,
        .qty,
        .notional,
        .status,
        .client_order_id
    ] |
    @tsv' |
    while IFS=$'\t' read -r created symbol side qty notional status client_order_id; do
        printf "%-20s %-8s %-6s %-8s %-10s %-12s %-50s\n" \
            "$created" "$symbol" "$side" "$qty" "$notional" "$status" "$client_order_id"
    done

echo "$(printf '%.0s-' {1..150})"
echo ""
echo "✅ Done"
echo ""
echo "💡 Tip: Run with --csv [filename] to export to CSV"
