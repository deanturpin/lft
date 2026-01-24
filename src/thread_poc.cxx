// Proof of concept: Multi-threaded architecture with proper coordination
// Just prints thread activity to verify timing and coordination

#include <chrono>
#include <format>
#include <mutex>
#include <print>
#include <semaphore>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

// Thread coordination primitives
auto calibration_complete = std::binary_semaphore{0}; // Blocks other threads
auto entry_exit_mutex = std::mutex{};                 // Mutual exclusion

// Helper to calculate next whole hour
auto calculate_next_hour(std::chrono::system_clock::time_point now) {
  auto now_t = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&now_t);
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_hour += 1; // Next hour
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// Helper to calculate next :35 past the minute
auto calculate_next_35_seconds() {
  auto now = std::chrono::system_clock::now();
  auto now_t = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&now_t);
  tm.tm_sec = 35;
  tm.tm_min += 1; // Next minute
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

int main() {
  std::println("🚀 LFT Threading Proof of Concept");
  std::println("Starting at {:%Y-%m-%d %H:%M:%S}\n",
               std::chrono::system_clock::now());

  // Thread 1: Calibration (runs once, sleeps until "end of session", then exits)
  // TEST: Using 1 second for calibration, then sleeping for 60 seconds total session
  auto calibration_thread = std::jthread([&](std::stop_token) {
    std::println("🎯 CALIBRATION THREAD: Starting");

    // Simulate calibration work (1 second for testing)
    std::this_thread::sleep_for(1s);

    std::println("🎯 CALIBRATION THREAD: Complete, releasing other threads");

    // Release other threads
    calibration_complete.release();

    // TEST: Sleep for 60 seconds total session (instead of until end of hour)
    constexpr auto test_session_duration = 60s;
    std::println("🎯 CALIBRATION THREAD: Sleeping for {} seconds (test session)",
                 std::chrono::duration_cast<std::chrono::seconds>(test_session_duration).count());

    // Interruptible sleep
    std::this_thread::sleep_for(test_session_duration);

    std::println(
        "🎯 CALIBRATION THREAD: Hour complete, requesting stop for other "
        "threads");

    // Exiting triggers jthread destructor which joins
  });

  // Thread 2: Entry logic (runs every 15 seconds for testing)
  auto entry_thread = std::jthread([&](std::stop_token stoken) {
    std::println("📥 ENTRY THREAD: Waiting for calibration...");
    calibration_complete.acquire();
    std::println("📥 ENTRY THREAD: Calibration complete, starting entry loop");

    while (not stoken.stop_requested()) {
      {
        auto lock = std::scoped_lock{entry_exit_mutex};
        std::println("📥 ENTRY THREAD: Evaluating signals at {:%H:%M:%S}",
                     std::chrono::system_clock::now());
        // Simulate work
        std::this_thread::sleep_for(500ms);
      } // Release mutex

      // TEST: Sleep 15 seconds between cycles (instead of waiting for :35)
      std::this_thread::sleep_for(15s);
    }

    std::println("📥 ENTRY THREAD: Stop requested, exiting");
  });

  // Thread 3: Exit logic (runs every 1 second for testing)
  auto exit_thread = std::jthread([&](std::stop_token stoken) {
    std::println("📤 EXIT THREAD: Waiting for calibration...");
    calibration_complete.acquire();
    calibration_complete.release(); // Re-release for entry thread
    std::println("📤 EXIT THREAD: Calibration complete, starting exit loop");

    while (not stoken.stop_requested()) {
      {
        auto lock = std::scoped_lock{entry_exit_mutex};
        std::println("📤 EXIT THREAD: Checking positions at {:%H:%M:%S}",
                     std::chrono::system_clock::now());
        // Simulate work
        std::this_thread::sleep_for(200ms);

        // Check for EOD liquidation (stub)
        auto now = std::chrono::system_clock::now();
        auto now_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&now_t);
        if (tm.tm_hour >= 15 and tm.tm_min >= 55) {
          std::println("📤 EXIT THREAD: EOD cutoff reached, liquidating all "
                       "positions");
          // Would close all positions here
        }
      } // Release mutex

      // TEST: Sleep 1 second between checks (instead of 10 seconds)
      std::this_thread::sleep_for(1s);
    }

    std::println("📤 EXIT THREAD: Stop requested, exiting");
  });

  // Main thread waits for calibration to exit
  std::println("🔄 MAIN THREAD: Waiting for calibration thread to complete...");
  calibration_thread.join(); // Wait for calibration to finish its hour

  std::println("🔄 MAIN THREAD: Calibration exited, requesting stop for other "
               "threads");
  entry_thread.request_stop();
  exit_thread.request_stop();

  std::println("🔄 MAIN THREAD: Waiting for all threads to join...");
  // jthread destructors auto-join

  std::println("\n✅ All threads completed cleanly");
  std::println("Session ended at {:%Y-%m-%d %H:%M:%S}",
               std::chrono::system_clock::now());

  return 0;
}
