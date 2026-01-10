# Trading Strategy Review Checklist

Use this as a **step-by-step review guide** for your strategy code and assumptions. The goal is to validate *edge*, *robustness*, and *execution reality* — not just signal logic.

---

## 1. Data & Inputs

* [ ] **What is one data point?**

  * Trade tick, quote update, or bar close?
  * Is the timestamp unique per update?

* [ ] **Are prices deduplicated safely?**

  * No dropping legitimate updates due to coarse timestamps

* [ ] **Are series aligned?**

  * Prices, highs, lows, volumes all advance together

* [ ] **Rolling window correctness**

  * Fixed-size buffer (ring buffer / deque)
  * No accidental lookback drift

---

## 2. Returns vs Prices (Critical)

* [ ] **Volatility is computed on returns, not price levels**

  * `(p_t / p_{t-1}) - 1` or `log(p_t/p_{t-1})`

* [ ] **Any z-score uses comparable units**

  * Mean and std dev derived from the same series

* [ ] **Thresholds scale across price regimes**

  * A rule that worked at 20k BTC also works at 60k

---

## 3. Signal Logic (Does it predict *magnitude*?)

For each strategy:

* [ ] **What horizon is this signal predicting?**

  * Next bar? Next hour? Same-day?

* [ ] **What is the expected move (in bps)?**

  * Average forward return after signal

* [ ] **Is this trend-following or mean-reverting?**

  * Does it fight momentum or join it?

* [ ] **Is it robust to noise?**

  * Single-tick moves do not dominate decisions

---

## 4. Cost Awareness (Edge Reality Check)

> **All costs and expected returns should be expressed in basis points (bps).**

**Reference:**

* 1 bp = 0.01% = 0.0001
* 10 bps = 0.1%
* 25 bps = 0.25%
* 100 bps = 1%

For a typical trade:

* [ ] **Expected gross move (bps)**
* [ ] **Spread (bps)**
* [ ] **Slippage (bps)**
* [ ] **Fees (bps)**
* [ ] **Adverse selection allowance (bps)**

Compute:

> **Net edge (bps) = expected move − total costs**

* [ ] Strategy disabled if net edge ≤ 0

---

## 5. Liquidity & Regime Filters

* [ ] **Low-liquidity detection**

  * Volume vs rolling average

* [ ] **Spread sanity check**

  * No trades when spread exceeds threshold

* [ ] **Volatility regime awareness**

  * Separate logic for quiet vs active markets

* [ ] **Calendar rules explicit**

  * Weekends / holidays either handled or avoided

---

## 6. Execution Model

* [ ] **Order type explicit**

  * Market vs limit vs hybrid

* [ ] **Fill assumptions realistic**

  * Partial fills, queue position, slippage modeled

* [ ] **Cooldowns and trade limits**

  * Prevent overtrading and signal clustering

---

## 7. Risk & Exit Logic

* [ ] **Stops defined in bps or ATR units**
* [ ] **Time-based exits present**
* [ ] **Asymmetric loss scenarios considered**

---

## 8. Review Outcome

* [ ] Can I state this strategy’s edge in **bps**?
* [ ] Do costs consume <50% of expected edge?
* [ ] Does it still work under worse spreads/slippage?

If you can’t answer these cleanly, the strategy isn’t finished — it’s just generating signals.
