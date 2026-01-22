# Breakfast Breakout Trading Strategy (5‑Minute Bars)

This document specifies a **Breakfast Breakout** trading strategy designed for systematic implementation in **C++**. The strategy uses 5‑minute bars, analyses the first hour after the regular session open, and uses the first **5 bars** to establish directional bias.

The goal is not to predict the entire day, but to classify **early orderflow** and trade **opening‑range breakouts** in the direction most likely to persist.

---

## 1. Core Assumptions

- **Bar timeframe:** 5 minutes (OHLCV)
- **Instrument:** liquid intraday market (equities, index futures, etc.)
- **Session open:** configurable (e.g. 09:30 New York for US equities)
- **First hour:** first 12 × 5‑minute bars
- **Bias window:** first **5 bars** (25 minutes)

---

## 2. Session Definitions

### 2.1 First Hour

Time window:

```
[session_open, session_open + 60 minutes)
```

- Bars indexed `0..11`

### 2.2 Opening Range (OR)

Computed after the first hour completes:

- `OR_high = max(high[0..11])`
- `OR_low  = min(low[0..11])`
- `OR_mid  = (OR_high + OR_low) / 2`
- `OR_size = OR_high - OR_low`

---

## 3. Breakfast Bias (First 5 Bars)

Bars used: `0..4`

### 3.1 Directional Metrics

- `net_move = close[4] - open[0]`
- `body_sum = Σ (close[i] - open[i]) , i=0..4`
- `up_bars = count(close[i] > open[i])`
- `down_bars = count(close[i] < open[i])`

### 3.2 Volatility / Quality Metrics

- `range5 = max(high[0..4]) - min(low[0..4])`

Optional (for chop detection):

- `wickiness = Σ ((high[i] - low[i]) - abs(close[i] - open[i]))`

High wickiness suggests indecision and lower confidence.

### 3.3 Bias Classification

Let `k1` be a tunable strength factor (default `0.35`).

**LONG bias** if:

- `up_bars >= 4`
- `net_move > 0`
- `abs(body_sum) >= k1 * range5`

**SHORT bias** if:

- `down_bars >= 4`
- `net_move < 0`
- `abs(body_sum) >= k1 * range5`

Otherwise:

- `bias = NEUTRAL`

> Bias is used as a *filter*, not a hard directional prediction.

---

## 4. Trade Entry Logic

Trades are only evaluated **after the first hour completes**.

### 4.1 Breakout Buffer

To avoid false breaks:

```
buffer = max(minBufferTicks, bufferFrac * OR_size)
```

Typical values:

- `bufferFrac = 0.02`
- `minBufferTicks = 1 tick`

---

### 4.2 Long Entry

Conditions:

- `bias != SHORT`
- `OR_size` within acceptable bounds
- A bar **closes above** the opening range

Trigger:

```
close[t] > OR_high + buffer
```

Entry methods:

- Market entry on breakout bar close
- OR stop entry at `OR_high + buffer`

---

### 4.3 Short Entry

Mirror logic:

- `bias != LONG`
- `close[t] < OR_low - buffer`

---

## 5. No‑Trade Filters

Skip the day entirely if:

- `OR_size < minOR` (range too small → chop)
- `OR_size > maxOR` (range too large → news / disorder)

Optional confirmations:

- Breakout bar volume > first‑hour average
- ATR‑relative OR size filter

---

## 6. Risk Management

### 6.1 Stop‑Loss Models

**A. Opening‑Range Stops**

- Long stop: `OR_mid` or `OR_low`
- Short stop: `OR_mid` or `OR_high`

**B. Volatility Stops**

```
stopDist = r * OR_size
```

Typical `r`: `0.25 – 0.5`

---

### 6.2 Profit Targets

**Fixed R‑multiple**

```
target = entry + R * (entry - stop)   // long
target = entry - R * (stop - entry)   // short
```

Typical `R`: `1.5 – 3.0`

**Alternative:**

- Partial exit at `+1R`
- Trail remainder using OR_mid or bar‑by‑bar lows/highs

---

### 6.3 Time Exit

To keep the strategy intraday:

- Exit all positions at:
  - `session_open + 4 hours`, or
  - end of session

---

## 7. Position Sizing

Risk‑based sizing:

```
riskPerTrade = accountEquity * riskPct
qty = floor(riskPerTrade / (abs(entry - stop) * pointValue))
```

Typical `riskPct`: `0.25% – 1.0%`

---

## 8. C++ Implementation Outline

### 8.1 Core Types

```cpp
struct Bar {
    int64_t ts;      // epoch millis
    double open, high, low, close;
    double volume;
};

enum class Bias { Long, Short, Neutral };
enum class Side { Buy, Sell, Flat };
```

---

### 8.2 Per‑Session State

```cpp
struct SessionState {
    bool inSession = false;
    int barIndex = 0;

    // first hour storage
    std::array<Bar, 12> firstHour;
    int firstHourCount = 0;

    // bias metrics
    int upBars = 0;
    int downBars = 0;
    double open0 = 0.0;
    double close4 = 0.0;
    double bodySum = 0.0;
    double hi5 = -INFINITY;
    double lo5 = INFINITY;

    Bias bias = Bias::Neutral;

    // opening range
    double orHigh = -INFINITY;
    double orLow = INFINITY;
    double orMid = 0.0;
    double orSize = 0.0;
    bool orReady = false;

    // position
    Side pos = Side::Flat;
    double entry = 0.0;
    double stop = 0.0;
    double target = 0.0;
};
```

---

### 8.3 Bar Processing Flow

1. Detect session start → reset `SessionState`
2. If bar in first hour:
   - store bar
   - update bias metrics if `barIndex < 5`
3. After bar 4 closes → compute `bias`
4. After bar 11 closes → compute opening range
5. For subsequent bars:
   - evaluate breakout entries
   - manage stops, targets, and time exits

---

## 9. Tunable Parameters

- `biasBars = 5`
- `openingRangeBars = 12`
- `k1` (bias strength)
- `bufferFrac`, `minBufferTicks`
- `minOR`, `maxOR`
- stop model selection
- `R` target multiple
- `timeExitMinutes`

---

## 10. Notes

- Handle timezones and DST explicitly
- Avoid look‑ahead bias (model fills correctly)
- Use exchange calendars for holidays / half days

---

**This spec is intentionally mechanical and testable.**
It is suitable for direct translation into a C++ event‑driven backtest or live trading engine.

