#include <ctime>
#include <iomanip>
#include <print>
#include <sstream>
#include <string_view>

// Parse ISO 8601 timestamp and convert to local time string
auto to_local_time(std::string_view iso_timestamp) -> std::string {
  // Parse ISO 8601: 2026-01-20T09:30:00-05:00
  std::tm tm = {};
  auto offset_hours = 0;
  auto offset_mins = 0;

  std::istringstream ss{std::string{iso_timestamp}};
  ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

  if (ss.fail())
    return std::string{iso_timestamp}; // Return original if parse fails

  // Parse timezone offset
  auto offset_sign = '+';
  ss >> offset_sign >> offset_hours;
  if (ss.peek() == ':')
    ss.ignore();
  ss >> offset_mins;

  // Convert to UTC by subtracting offset
  auto utc_time = std::mktime(&tm);
  if (offset_sign == '-')
    utc_time += (offset_hours * 3600 + offset_mins * 60);
  else
    utc_time -= (offset_hours * 3600 + offset_mins * 60);

  // Convert to local time
  auto local_tm = *std::localtime(&utc_time);

  // Format as readable string
  std::ostringstream out{};
  out << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S %Z");
  return out.str();
}

int main() {
  std::println("Testing timezone conversion\n");

  auto et_time = "2026-01-20T09:30:00-05:00";
  std::println("ET time:    {}", et_time);
  std::println("Local time: {}", to_local_time(et_time));

  return 0;
}
