# Strategy Review & Next-Step Recommendations

This document reviews the **current strategy set and supporting structures**, identifies **key correctness and control issues**, and proposes **incremental improvements and new strategies** aligned with the current system philosophy:

> **Intraday, time-boxed trading with wide risk limits and forced EOD liquidation**

The goal is not to add complexity, but to **stabilise behaviour, improve interpretability, and protect the v2 edge** you have already uncovered.

---

## 1. High-Level Assessment

Your current strategy basket is a solid foundation:

- Mean Reversion
- Moving Average Crossover
- Volatility Breakout
- Relative Strength
- Volume Surge
- Simple Dip Detector

Together, these cover:
- shock / dislocation
- trend initiation
- volatility expansion
- cross-sectional strength
- participation-driven moves

Crucially, these strategies now operate under a **time-dominant exit regime (EOD liquidation)**, which changes how signals should be evaluated and filtered.

---

## 2. Data & Plumbing Issues (Important to Fix)

### 2.1 Vector Trimming Performance

Current implementation repeatedly does:

```
prices.erase(prices.begin())
```

This is **O(n)** and will scale poorly with many symbols. Under load, it can subtly change behaviour due to timing drift.

**Recommendation:**
- Switch to a ring buffer or `std::deque`
- Or maintain a rolling index and compact occasionally

---

### 2.2 MA Crossover Copies Price History

`evaluate_ma_crossover()` copies the entire price vector to compute prior MAs.

**Impact:**
- unnecessary allocations
- unnecessary CPU

**Recommendation:**
- compute previous MA directly via indices or rolling sums

---

### 2.3 Volatility Calculation Consistency

- `volatility()` uses std-dev of returns
- recent volatility uses average absolute returns over a very short window

These are not statistically comparable.

**Recommendation:**
- Use a single volatility family (e.g. ATR)
- Compare ATR(5) vs ATR(20) for breakout logic

---

### 2.4 Assertions in Live Code

`assert()` is excellent during development but dangerous in production (compiled out or process-killing).

**Recommendation:**
- keep assertions for invariants during dev
- convert runtime data issues into logged rejections, not crashes

---

### 2.5 Volume Ratio Is Not Time-Normalised

Current `avg_volume()` compares against a rolling mean of recent bars.

**Problem:**
- ignores strong intraday seasonality
- misclassifies lunch vs open vs close

**Recommendation:**
- normalise volume by expected volume at the same time of day
- at minimum, use a rolling median instead of mean

---

## 3. Strategy-Specific Review & Improvements

### 3.1 Dip Strategy

**Current:** single-bar percentage drop

**Issue:** twitchy, overlaps mean reversion

**Upgrade:**
- require stabilisation (e.g. next bar closes above midpoint)
- or fold dip detection into mean reversion as a feature

---

### 3.2 MA Crossover (5 / 20)

**Strength:** simple trend timing

**Weakness:** chop sensitivity

**Upgrades:**
- require positive slope of long MA
- gate by `recent_noise()`
- optional price-above-long-MA filter

---

### 3.3 Mean Reversion

**Status:** fundamentally corrected after Z-score fix

**Remaining improvements:**
- gate by trend regime (avoid MR against strong trends)
- enforce entry cutoff near EOD (handled at orchestrator level)

---

### 3.4 Volatility Breakout

**Current:** short-term vol vs long-term vol

**Issue:** mismatched definitions

**Upgrade:**
- use ATR-based compression → expansion
- combine with range break confirmation

---

### 3.5 Relative Strength

**Current:** compares against universe average

**Issue:** universe composition bias

**Upgrade:**
- compare against a benchmark (SPY / QQQ)
- use multi-bar return instead of single-bar change

---

### 3.6 Volume Surge

**Status:** well-defined

**Upgrade:**
- require range expansion alongside volume
- avoid high-volume / no-movement stalls

---

## 4. Structural Improvements (High Leverage, Low Risk)

### 4.1 Signal Semantics

Add explicit direction and intent:

- Signal side (Buy / Sell / None)
- Signal type (Entry / Exit / Filter)

This future-proofs the system and avoids refactors later.

---

### 4.2 Confidence Semantics

Currently undefined.

**Recommendation:**
- use confidence for **ranking / tie-breaking**, not sizing
- avoid hard thresholds until behaviour is well understood

---

### 4.3 Strategy Config vs Runtime State

Calibration metrics (win rate, expected move, etc.) should be treated as **read-only inputs**, not mutable config.

**Recommendation:**
- keep config pure
- store calibration and live performance in `StrategyStats`

---

### 4.4 Median Duration Is Not a Median

Current `median_duration_bars()` computes a mean.

**Fix:**
- track durations explicitly
- compute true median or approximate quantiles

This matters now that **time-in-trade is the primary risk factor**.

---

### 4.5 Explicit Eligibility Object

Centralise entry gating logic (spread, volume, time-to-EOD):

- makes rejections explainable
- avoids hidden interactions
- simplifies debugging

---

## 5. Strategies Worth Adding Next (Aligned with EOD Edge)

### 5.1 Opening Range Breakout (ORB)

- first-hour range
- bias filter
- hold to EOD

Matches your original "breakfast breakout" idea perfectly.

---

### 5.2 VWAP Trend + Pullback

- trend defined by VWAP slope
- buy pullbacks
- exit EOD

Very compatible with wide stops and time-based exits.

---

### 5.3 Volatility Squeeze → Expansion

- Bollinger bandwidth or ATR compression
- expansion trigger

Cleaner alternative to current vol breakout logic.

---

### 5.4 Market Regime Gate (Meta-Strategy)

Not a strategy, but a **global filter**:

- trade less on dead days
- require minimum index range or VWAP slope

This is often higher ROI than adding new entry logic.

---

## 6. Immediate Next Steps (Recommended Order)

1. Add **entry cutoff before EOD** (20–30 min)
2. Fix **median duration calculation**
3. Remove vector-copy hot paths
4. Time-normalise volume ratio
5. Add ORB or VWAP-based strategy

Do **not** retune exits yet — your current exit architecture is correct.

---

## 7. Final Notes

You are now past the "indicator tweaking" phase.

The system’s edge is:

- directional correctness + time
- wide tolerance for noise
- strict risk containment

Everything recommended here is about **control, observability, and robustness**, not squeezing more backtest P&L.

This document should serve as a reference point while stabilising v2 behaviour on `main`.

