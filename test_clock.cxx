#include "alpaca_client.h"
#include <print>

int main() {
  std::println("🕐 Testing Alpaca Clock API");

  auto client = lft::AlpacaClient{};

  auto result = client.get_market_clock();

  if (result) {
    const auto& clock = *result;
    std::println("✅ Market Clock:");
    std::println("   Current Time:  {}", clock.timestamp);
    std::println("   Market Open:   {}", clock.is_open ? "YES" : "NO");
    std::println("   Next Open:     {}", clock.next_open);
    std::println("   Next Close:    {}", clock.next_close);
  } else {
    std::println("❌ API check failed");
  }

  return 0;
}
