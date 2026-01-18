#include "alpaca_client.h"
#include <cstdlib>
#include <format>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <print>

using json = nlohmann::json;

namespace lft {

AlpacaClient::AlpacaClient()
    : api_key_{get_env_or_default("ALPACA_API_KEY", "")},
      api_secret_{get_env_or_default("ALPACA_API_SECRET", "")},
      base_url_{get_env_or_default("ALPACA_BASE_URL",
                                   "https://paper-api.alpaca.markets")},
      data_url_{
          get_env_or_default("ALPACA_DATA_URL", "https://data.alpaca.markets")},
      data_api_key_{get_env_or_default("ALPACA_DATA_API_KEY", api_key_)},
      data_api_secret_{
          get_env_or_default("ALPACA_DATA_API_SECRET", api_secret_)} {}

std::string AlpacaClient::get_env_or_default(std::string_view name,
                                             std::string_view default_val) {
  if (const auto *val = std::getenv(name.data()))
    return val;
  return std::string{default_val};
}

std::expected<std::map<std::string, Snapshot>, AlpacaError>
AlpacaClient::get_snapshots(const std::vector<std::string> &symbols) {

  // Build comma-separated symbol list
  auto symbol_list = std::string{};
  for (auto i = 0uz; i < symbols.size(); ++i) {
    if (i > 0)
      symbol_list += ",";
    symbol_list += symbols[i];
  }

  // Create HTTPS client for market data API
  auto client = httplib::Client{data_url_};
  client.set_connection_timeout(10);
  client.set_read_timeout(30);

  // Build request path
  auto path = std::format("/v2/stocks/snapshots?symbols={}", symbol_list);

  // Set headers
  httplib::Headers headers = {{"APCA-API-KEY-ID", api_key_},
                              {"APCA-API-SECRET-KEY", api_secret_}};

  auto res = client.Get(path, headers);

  if (not res) {
    std::println(stderr, "  Network error - no response");
    return std::unexpected(AlpacaError::NetworkError);
  }

  if (res->status == 401)
    return std::unexpected(AlpacaError::AuthError);

  if (res->status == 429)
    return std::unexpected(AlpacaError::RateLimitError);

  if (res->status != 200) {
    // Debug: print error response
    std::println(stderr, "Snapshots API error: status={}, body={}", res->status,
                 res->body);
    return std::unexpected(AlpacaError::UnknownError);
  }

  try {
    auto j = json::parse(res->body);

    auto snapshots = std::map<std::string, Snapshot>{};

    for (const auto &[symbol, data] : j.items()) {
      auto snap = Snapshot{};
      snap.symbol = symbol;

      if (data.contains("latestTrade") and not data["latestTrade"].is_null()) {
        snap.latest_trade_price = data["latestTrade"]["p"];
        snap.latest_trade_timestamp = data["latestTrade"]["t"];
      }

      if (data.contains("latestQuote") and not data["latestQuote"].is_null()) {
        snap.latest_quote_bid = data["latestQuote"]["bp"];
        snap.latest_quote_ask = data["latestQuote"]["ap"];
      }

      if (data.contains("prevDailyBar") and not data["prevDailyBar"].is_null())
        snap.prev_daily_bar_close = data["prevDailyBar"]["c"];

      // Extract volume from minute bar for volume filtering
      if (data.contains("minuteBar") and not data["minuteBar"].is_null())
        snap.minute_bar_volume = data["minuteBar"]["v"];

      snapshots[symbol] = snap;
    }

    return snapshots;

  } catch (const json::exception &) {
    return std::unexpected(AlpacaError::ParseError);
  }
}

std::optional<Snapshot> AlpacaClient::get_snapshot(std::string_view symbol) {
  auto result = get_snapshots({std::string{symbol}});
  if (not result)
    return std::nullopt;

  auto &snapshots = result.value();
  if (snapshots.empty())
    return std::nullopt;

  return snapshots.begin()->second;
}

std::expected<std::map<std::string, Snapshot>, AlpacaError>
AlpacaClient::get_crypto_snapshots(const std::vector<std::string> &symbols) {

  // Build comma-separated symbol list
  auto symbol_list = std::string{};
  for (auto i = 0uz; i < symbols.size(); ++i) {
    if (i > 0)
      symbol_list += ",";
    symbol_list += symbols[i];
  }

  // Create HTTPS client for market data API
  auto client = httplib::Client{data_url_};
  client.set_connection_timeout(10);
  client.set_read_timeout(30);

  // Build request path for crypto (v1beta3)
  auto path =
      std::format("/v1beta3/crypto/us/snapshots?symbols={}", symbol_list);

  // Set headers
  httplib::Headers headers = {{"APCA-API-KEY-ID", api_key_},
                              {"APCA-API-SECRET-KEY", api_secret_}};

  auto res = client.Get(path, headers);

  if (not res) {
    std::println(stderr, "  Network error - no response");
    return std::unexpected(AlpacaError::NetworkError);
  }

  if (res->status == 401)
    return std::unexpected(AlpacaError::AuthError);

  if (res->status == 429)
    return std::unexpected(AlpacaError::RateLimitError);

  if (res->status != 200) {
    // Debug: print error response
    std::println(stderr, "Crypto snapshots API error: status={}, body={}",
                 res->status, res->body);
    return std::unexpected(AlpacaError::UnknownError);
  }

  try {
    auto j = json::parse(res->body);

    auto snapshots = std::map<std::string, Snapshot>{};

    // Crypto response has "snapshots" object containing symbol data
    if (j.contains("snapshots")) {
      for (const auto &[symbol, data] : j["snapshots"].items()) {
        auto snap = Snapshot{};
        snap.symbol = symbol;

        if (data.contains("latestTrade") and
            not data["latestTrade"].is_null()) {
          snap.latest_trade_price = data["latestTrade"]["p"];
          snap.latest_trade_timestamp = data["latestTrade"]["t"];
        }

        if (data.contains("latestQuote") and
            not data["latestQuote"].is_null()) {
          snap.latest_quote_bid = data["latestQuote"]["bp"];
          snap.latest_quote_ask = data["latestQuote"]["ap"];
        }

        if (data.contains("prevDailyBar") and
            not data["prevDailyBar"].is_null())
          snap.prev_daily_bar_close = data["prevDailyBar"]["c"];

        // Extract volume from minute bar for volume filtering
        if (data.contains("minuteBar") and not data["minuteBar"].is_null())
          snap.minute_bar_volume = data["minuteBar"]["v"];

        snapshots[symbol] = snap;
      }
    }

    return snapshots;

  } catch (const json::exception &) {
    return std::unexpected(AlpacaError::ParseError);
  }
}

std::expected<std::string, AlpacaError> AlpacaClient::get_account() {
  auto client = httplib::Client{base_url_};
  client.set_connection_timeout(10);
  client.set_read_timeout(30);

  httplib::Headers headers = {{"APCA-API-KEY-ID", api_key_},
                              {"APCA-API-SECRET-KEY", api_secret_}};

  auto res = client.Get("/v2/account", headers);

  if (not res)
    return std::unexpected(AlpacaError::NetworkError);

  if (res->status == 401)
    return std::unexpected(AlpacaError::AuthError);

  if (res->status != 200) {
    std::println(stderr, "API error: status={}, body={}", res->status,
                 res->body);
    return std::unexpected(AlpacaError::UnknownError);
  }

  return res->body;
}

std::vector<Position> AlpacaClient::get_positions() {
  auto client = httplib::Client{base_url_};
  client.set_connection_timeout(10);
  client.set_read_timeout(30);

  httplib::Headers headers = {{"APCA-API-KEY-ID", api_key_},
                              {"APCA-API-SECRET-KEY", api_secret_}};

  auto res = client.Get("/v2/positions", headers);

  if (not res or res->status != 200)
    return {};

  auto positions = std::vector<Position>{};
  auto json = nlohmann::json::parse(res->body, nullptr, false);

  if (json.is_discarded() or not json.is_array())
    return {};

  for (const auto &item : json) {
    auto pos = Position{};
    pos.symbol = item.value("symbol", "");
    pos.qty = std::stod(item.value("qty", "0"));
    pos.avg_entry_price = std::stod(item.value("avg_entry_price", "0"));
    pos.current_price = std::stod(item.value("current_price", "0"));
    pos.unrealized_pl = std::stod(item.value("unrealized_pl", "0"));
    pos.unrealized_plpc = std::stod(item.value("unrealized_plpc", "0"));
    positions.push_back(pos);
  }

  return positions;
}

std::expected<std::string, AlpacaError> AlpacaClient::get_open_orders() {
  auto client = httplib::Client{base_url_};
  client.set_connection_timeout(10);
  client.set_read_timeout(30);

  httplib::Headers headers = {{"APCA-API-KEY-ID", api_key_},
                              {"APCA-API-SECRET-KEY", api_secret_}};

  auto res = client.Get("/v2/orders?status=open", headers);

  if (not res)
    return std::unexpected(AlpacaError::NetworkError);

  if (res->status == 401)
    return std::unexpected(AlpacaError::AuthError);

  if (res->status != 200) {
    std::println(stderr, "API error: status={}, body={}", res->status,
                 res->body);
    return std::unexpected(AlpacaError::UnknownError);
  }

  return res->body;
}

std::expected<std::string, AlpacaError> AlpacaClient::get_all_orders() {
  auto client = httplib::Client{base_url_};
  client.set_connection_timeout(30); // Longer timeout for potentially large response
  client.set_read_timeout(60); // Large response may take time

  httplib::Headers headers = {{"APCA-API-KEY-ID", api_key_},
                              {"APCA-API-SECRET-KEY", api_secret_}};

  // Get all orders (limit=100 - enough for position recovery and cooldown)
  auto res = client.Get("/v2/orders?status=all&limit=100", headers);

  if (not res)
    return std::unexpected(AlpacaError::NetworkError);

  if (res->status == 401)
    return std::unexpected(AlpacaError::AuthError);

  if (res->status != 200) {
    std::println(stderr, "API error: status={}, body={}", res->status,
                 res->body);
    return std::unexpected(AlpacaError::UnknownError);
  }

  return res->body;
}

std::expected<std::string, AlpacaError>
AlpacaClient::place_order(std::string_view symbol, std::string_view side,
                          double notional, std::string_view client_order_id) {

  auto client = httplib::Client{base_url_};
  client.set_connection_timeout(10);
  client.set_read_timeout(15); // Fail fast for order placement

  httplib::Headers headers = {{"APCA-API-KEY-ID", api_key_},
                              {"APCA-API-SECRET-KEY", api_secret_},
                              {"Content-Type", "application/json"}};

  // Crypto symbols contain '/', use gtc for crypto, day for stocks
  auto is_crypto = std::string{symbol}.find('/') != std::string::npos;
  auto time_in_force = is_crypto ? "gtc" : "day";

  // Build order JSON
  auto order = json{{"symbol", symbol},
                    {"side", side},
                    {"type", "market"},
                    {"time_in_force", time_in_force},
                    {"notional", notional}};

  // Add client_order_id if provided
  if (not client_order_id.empty())
    order["client_order_id"] = client_order_id;

  auto res =
      client.Post("/v2/orders", headers, order.dump(), "application/json");

  if (not res)
    return std::unexpected(AlpacaError::NetworkError);

  if (res->status == 401)
    return std::unexpected(AlpacaError::AuthError);

  if (res->status == 403 or res->status == 422) {
    std::println(stderr, "Order rejected: status={}, body={}", res->status,
                 res->body);
    return std::unexpected(AlpacaError::UnknownError);
  }

  if (res->status != 200) {
    std::println(stderr, "Order API error: status={}, body={}", res->status,
                 res->body);
    return std::unexpected(AlpacaError::UnknownError);
  }

  return res->body;
}

std::expected<std::string, AlpacaError>
AlpacaClient::place_order_qty(std::string_view symbol, std::string_view side,
                              double quantity, std::string_view client_order_id) {

  auto client = httplib::Client{base_url_};
  client.set_connection_timeout(10);
  client.set_read_timeout(15); // Fail fast for order placement

  httplib::Headers headers = {{"APCA-API-KEY-ID", api_key_},
                              {"APCA-API-SECRET-KEY", api_secret_},
                              {"Content-Type", "application/json"}};

  // Crypto symbols contain '/', use gtc for crypto, day for stocks
  auto is_crypto = std::string{symbol}.find('/') != std::string::npos;
  auto time_in_force = is_crypto ? "gtc" : "day";

  // Build order JSON using quantity (not notional)
  auto order = json{{"symbol", symbol},
                    {"side", side},
                    {"type", "market"},
                    {"time_in_force", time_in_force},
                    {"qty", quantity}};

  // Add client_order_id if provided
  if (not client_order_id.empty())
    order["client_order_id"] = client_order_id;

  auto res =
      client.Post("/v2/orders", headers, order.dump(), "application/json");

  if (not res)
    return std::unexpected(AlpacaError::NetworkError);

  if (res->status == 401)
    return std::unexpected(AlpacaError::AuthError);

  if (res->status == 403 or res->status == 422) {
    std::println(stderr, "Order rejected: status={}, body={}", res->status,
                 res->body);
    return std::unexpected(AlpacaError::UnknownError);
  }

  if (res->status != 200) {
    std::println(stderr, "Order API error: status={}, body={}", res->status,
                 res->body);
    return std::unexpected(AlpacaError::UnknownError);
  }

  return res->body;
}

std::expected<std::string, AlpacaError>
AlpacaClient::close_position(std::string_view symbol) {
  auto client = httplib::Client{base_url_};
  client.set_connection_timeout(10);
  client.set_read_timeout(15); // Fail fast for position closing

  httplib::Headers headers = {{"APCA-API-KEY-ID", api_key_},
                              {"APCA-API-SECRET-KEY", api_secret_}};

  auto path = std::format("/v2/positions/{}", symbol);
  auto res = client.Delete(path, headers);

  if (not res)
    return std::unexpected(AlpacaError::NetworkError);

  if (res->status == 401)
    return std::unexpected(AlpacaError::AuthError);

  if (res->status == 404) {
    std::println(stderr, "Position not found: {}", symbol);
    return std::unexpected(AlpacaError::UnknownError);
  }

  if (res->status != 200) {
    std::println(stderr, "Close position error: status={}, body={}",
                 res->status, res->body);
    return std::unexpected(AlpacaError::UnknownError);
  }

  return res->body;
}

std::expected<std::vector<Bar>, AlpacaError>
AlpacaClient::get_bars(std::string_view symbol, std::string_view timeframe,
                       std::string_view start, std::string_view end) {

  auto client = httplib::Client{data_url_};
  client.set_connection_timeout(30);
  client.set_read_timeout(60); // Historical data can be large

  // Build request path for stock bars (using IEX feed for free tier)
  auto path = std::format(
      "/v2/stocks/{}/bars?timeframe={}&start={}&end={}&limit=10000&feed=iex",
      symbol, timeframe, start, end);

  httplib::Headers headers = {{"APCA-API-KEY-ID", data_api_key_},
                              {"APCA-API-SECRET-KEY", data_api_secret_}};

  auto res = client.Get(path, headers);

  if (not res)
    return std::unexpected(AlpacaError::NetworkError);

  if (res->status == 401)
    return std::unexpected(AlpacaError::AuthError);

  if (res->status == 404)
    return std::unexpected(AlpacaError::InvalidSymbol);

  if (res->status != 200)
    return std::unexpected(AlpacaError::UnknownError);

  // Parse bars from response
  auto data_result = json::parse(res->body, nullptr, false);
  if (data_result.is_discarded())
    return std::unexpected(AlpacaError::ParseError);

  auto bars = std::vector<Bar>{};
  if (not data_result.contains("bars"))
    return bars;

  for (const auto &bar_json : data_result["bars"]) {
    auto bar = Bar{};
    bar.timestamp = bar_json["t"].get<std::string>();
    bar.open = bar_json["o"].get<double>();
    bar.high = bar_json["h"].get<double>();
    bar.low = bar_json["l"].get<double>();
    bar.close = bar_json["c"].get<double>();
    bar.volume = bar_json["v"].get<long>();
    bars.push_back(bar);
  }

  return bars;
}

std::optional<std::vector<Bar>> AlpacaClient::get_bars(std::string_view symbol,
                                                        std::string_view timeframe,
                                                        int days) {
  // Calculate start and end dates
  const auto now = std::chrono::system_clock::now();
  const auto start = now - std::chrono::hours(24 * days);

  const auto end_t = std::chrono::system_clock::to_time_t(now);
  const auto start_t = std::chrono::system_clock::to_time_t(start);

  auto end_tm = *std::gmtime(&end_t);
  auto start_tm = *std::gmtime(&start_t);

  const auto end_str = std::format("{:04}-{:02}-{:02}", end_tm.tm_year + 1900,
                                    end_tm.tm_mon + 1, end_tm.tm_mday);
  const auto start_str = std::format("{:04}-{:02}-{:02}", start_tm.tm_year + 1900,
                                      start_tm.tm_mon + 1, start_tm.tm_mday);

  auto result = get_bars(symbol, timeframe, start_str, end_str);
  if (not result)
    return std::nullopt;

  return result.value();
}

std::expected<std::vector<Bar>, AlpacaError>
AlpacaClient::get_crypto_bars(std::string_view symbol,
                              std::string_view timeframe,
                              std::string_view start, std::string_view end) {

  auto client = httplib::Client{data_url_};
  client.set_connection_timeout(30);
  client.set_read_timeout(60); // Historical data can be large

  // Build request path for crypto bars
  auto path =
      std::format("/v1beta3/crypto/us/"
                  "bars?symbols={}&timeframe={}&start={}&end={}&limit=10000",
                  symbol, timeframe, start, end);

  httplib::Headers headers = {{"APCA-API-KEY-ID", data_api_key_},
                              {"APCA-API-SECRET-KEY", data_api_secret_}};

  auto res = client.Get(path, headers);

  if (not res)
    return std::unexpected(AlpacaError::NetworkError);

  if (res->status == 401)
    return std::unexpected(AlpacaError::AuthError);

  if (res->status == 404)
    return std::unexpected(AlpacaError::InvalidSymbol);

  if (res->status != 200)
    return std::unexpected(AlpacaError::UnknownError);

  // Parse bars from response
  auto data_result = json::parse(res->body, nullptr, false);
  if (data_result.is_discarded())
    return std::unexpected(AlpacaError::ParseError);

  auto bars = std::vector<Bar>{};
  if (not data_result.contains("bars") or
      not data_result["bars"].contains(std::string{symbol}))
    return bars;

  for (const auto &bar_json : data_result["bars"][std::string{symbol}]) {
    auto bar = Bar{};
    bar.timestamp = bar_json["t"].get<std::string>();
    bar.open = bar_json["o"].get<double>();
    bar.high = bar_json["h"].get<double>();
    bar.low = bar_json["l"].get<double>();
    bar.close = bar_json["c"].get<double>();
    bar.volume = bar_json["v"].get<long>();
    bars.push_back(bar);
  }

  return bars;
}

} // namespace lft
