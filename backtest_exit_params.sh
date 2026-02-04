#!/bin/bash
# Test different TP/SL combinations to find optimal risk/reward ratios

set -e

# Save original values
ORIGINAL_FILE="include/defs.h"
BACKUP_FILE="include/defs.h.backup"
CALIBRATE_FILE="src/calibrate.cxx"
CALIBRATE_BACKUP="src/calibrate.cxx.backup"

# Backup original files
cp "$ORIGINAL_FILE" "$BACKUP_FILE"
cp "$CALIBRATE_FILE" "$CALIBRATE_BACKUP"

# Re-enable mean reversion for backtesting
echo "Re-enabling mean reversion for backtesting..."
sed -i.tmp 's/"ma_crossover", \/\* "mean_reversion", \*\//"ma_crossover", "mean_reversion",/' "$CALIBRATE_FILE"
rm -f "${CALIBRATE_FILE}.tmp"

echo "Testing different TP/SL parameter combinations..."
echo "=================================================="
echo ""

# Test configurations: TP/SL/TS (as decimal percentages, e.g., 0.02 = 2%)
# Format: "TP SL TS DESCRIPTION"
CONFIGS=(
    "0.02 0.01 0.009 Original_2-1-0.9"
    "0.02 0.02 0.018 Symmetric_2-2-1.8"
    "0.03 0.03 0.027 Symmetric_3-3-2.7"
    "0.02 0.03 0.018 Conservative_2-3-1.8"
    "0.03 0.02 0.027 Aggressive_3-2-2.7"
    "1.0 0.03 0.02 Current_disabled-3-2"
)

# Store results
RESULTS_FILE="backtest_results_$(date +%Y%m%d_%H%M%S).txt"

for config in "${CONFIGS[@]}"; do
    read -r tp sl ts desc <<< "$config"

    echo "----------------------------------------"
    echo "Testing: $desc (TP:${tp} SL:${sl} TS:${ts})"
    echo "----------------------------------------"

    # Update defs.h with new values
    sed -i.tmp "s/^constexpr auto take_profit_pct = .*;/constexpr auto take_profit_pct = ${tp};/" "$ORIGINAL_FILE"
    sed -i.tmp "s/^constexpr auto stop_loss_pct = .*;/constexpr auto stop_loss_pct = ${sl};/" "$ORIGINAL_FILE"
    sed -i.tmp "s/^constexpr auto trailing_stop_pct = .*;/constexpr auto trailing_stop_pct = ${ts};/" "$ORIGINAL_FILE"
    rm -f "${ORIGINAL_FILE}.tmp"

    # Rebuild
    cd build
    make -j lft > /dev/null 2>&1
    cd ..

    # Run lft (will do calibration then try to trade, so kill after calibration)
    # Source .env for API keys, run with timeout (calibration takes ~10s)
    source .env
    ./build/lft > /tmp/lft_output.txt 2>&1 &
    LFT_PID=$!
    sleep 15  # Wait for calibration to complete
    kill $LFT_PID 2>/dev/null || true
    wait $LFT_PID 2>/dev/null || true
    OUTPUT=$(cat /tmp/lft_output.txt)

    # Extract calibration summary
    CALIBRATION=$(echo "$OUTPUT" | grep -A 20 "📊 Calibration complete:")

    # Print calibration output
    echo "$CALIBRATION"
    echo ""

    # Extract total P&L across all strategies
    TOTAL_PL=$(echo "$CALIBRATION" | grep "ENABLED" | awk '{sum += $4} END {printf "%.2f", sum}')

    # Save to results file
    echo "$desc,${tp},${sl},${ts},${TOTAL_PL:-0}" >> "$RESULTS_FILE"
done

# Restore original files
mv "$BACKUP_FILE" "$ORIGINAL_FILE"
mv "$CALIBRATE_BACKUP" "$CALIBRATE_FILE"

# Rebuild with original settings
cd build
make -j lft > /dev/null 2>&1
cd ..

echo "=========================================="
echo "Testing complete!"
echo "Results saved to: $RESULTS_FILE"
echo "Original settings restored"
echo ""
echo "Summary (sorted by final capital):"
echo "Config,TP,SL,TS,Trades,WinRate,FinalCapital,AvgReturn"
sort -t',' -k7 -rn "$RESULTS_FILE" | column -t -s','
