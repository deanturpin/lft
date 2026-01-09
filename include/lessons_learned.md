# Lessons learned

- Accidentally bought 100K of crypto instead of 6K (6 * nominal 1K) as the API is different for cryptos
- Exit strategy can fire in out of hours for stocks but doesn't get filled... consider closing all positions at end of trading
- Would be cool if you could add notes to the trade when you open on Alpaca but all the info needs to be managed at our end (which presents a problem because we wipe clean every hour)
- Have gone for a unified exit strategy after playing around with a much more complicated per entry strategy implementation: long running trades just wanna get out as the original calibration results may no longer be valid
