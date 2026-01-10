# Prioritised Strategy Improvement Actions

This is a **practical, ordered action list** you can follow without second-guessing. Stop when you hit diminishing returns — you don’t need everything to move forward safely.

---

## Tier 1 — Must Do (Correctness & Survival)

These fix issues that can silently invalidate results.

1. **Switch all volatility calculations to returns-based**
   - Replace price-level standard deviation with returns or log-returns
   - Update mean reversion, breakout, and any risk sizing logic
   - Outcome: thresholds mean the same thing across price regimes

2. **Standardise everything into basis points (bps)**
   - Expected move, spread, slippage, fees, stops
   - Add helper functions: `price_to_bps()`, `bps_to_price()`
   - Outcome: edge becomes measurable and comparable

3. **Make trade eligibility explicit**
   - Hard block trades when:
     - Spread > max allowed bps
     - Volume below minimum threshold
   - Outcome: prevents trading when the math cannot work

---

## Tier 2 — Edge Reality (Profitability)

These determine whether signals survive contact with execution.

4. **Estimate costs pessimistically**
   - Use *worse-than-average* spread and slippage assumptions
   - Bake this into pre-trade checks
   - Outcome: fewer trades, higher quality

5. **Quantify expected move per signal**
   - Measure average forward return after signal (in bps)
   - Do not rely on hit-rate alone
   - Outcome: know which strategies actually have edge

6. **Add cooldowns and trade limits**
   - Min time between entries per symbol
   - Max trades per day/session
   - Outcome: avoids overtrading and signal clustering

---

## Tier 3 — Regime Awareness (Stability)

These reduce drawdowns and strategy decay.

7. **Add a simple regime classifier**
   - Low / normal / high liquidity
   - Quiet / normal / volatile
   - Outcome: strategies run only where they make sense

8. **Explicitly disable strategies in bad regimes**
   - Weekends, news spikes, ultra-low volume
   - Do nothing is a valid state
   - Outcome: protects edge without adding complexity

---

## Tier 4 — Risk & Exits (Capital Protection)

These shape the equity curve more than entries.

9. **Define stops in bps or ATR units**
   - No price-level magic numbers
   - Stops scale with volatility

10. **Add time-based exits**
    - Especially for mean reversion
    - Exit if nothing happens

11. **Check asymmetry of losses**
    - Occasional big losses > many small wins = hidden negative edge

---

## Tier 5 — Engineering Hygiene (Optional but Valuable)

Do these when you want to scale or speed up iteration.

12. **Replace vector front-erases with ring buffers**
    - Avoid O(N) operations on hot paths

13. **Unify tick vs bar handling**
    - Be explicit about which drives strategy logic

14. **Log everything in bps**
    - Signal strength
    - Entry cost
    - Realised vs expected edge

---

## Stopping Rule (Important)

You can stop when:

- [ ] You can state expected edge per trade in **bps**
- [ ] Costs are explicitly modeled and conservative
- [ ] Strategy self-disables in bad conditions

Beyond this, improvements are incremental — not existential.

---

## Mental Model

> *Entries are easy.*  
> *Edge is rare.*  
> *Survival is design.*

This list is designed to get you to the third line as quickly as possible.
