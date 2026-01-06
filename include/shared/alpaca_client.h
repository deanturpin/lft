#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>
#include <map>

namespace lft {

struct Quote {
    std::string symbol;
    double bid_price{};
    double ask_price{};
    double last_price{};
    long bid_size{};
    long ask_size{};
    std::string timestamp;
};

struct Snapshot {
    std::string symbol;
    double latest_trade_price{};
    double latest_quote_bid{};
    double latest_quote_ask{};
    double prev_daily_bar_close{};
    std::string latest_trade_timestamp;
};

enum class AlpacaError {
    NetworkError,
    AuthError,
    RateLimitError,
    InvalidSymbol,
    ParseError,
    UnknownError
};

class AlpacaClient {
public:
    AlpacaClient();

    // Check if client has valid credentials
    bool is_valid() const { return not api_key_.empty() and not api_secret_.empty(); }

    // Get latest quotes for stock symbols
    std::expected<std::map<std::string, Snapshot>, AlpacaError>
    get_snapshots(const std::vector<std::string>&);

    // Get latest quotes for crypto symbols
    std::expected<std::map<std::string, Snapshot>, AlpacaError>
    get_crypto_snapshots(const std::vector<std::string>&);

    // Get account information
    std::expected<std::string, AlpacaError> get_account();

    // Get all open positions
    std::expected<std::string, AlpacaError> get_positions();

    // Place a market order (notional = dollar amount for fractional shares)
    std::expected<std::string, AlpacaError> place_order(std::string_view, std::string_view, double);

    // Close a position by symbol
    std::expected<std::string, AlpacaError> close_position(std::string_view);

private:
    std::string api_key_;
    std::string api_secret_;
    std::string base_url_;
    std::string data_url_;

    std::string get_env_or_default(std::string_view, std::string_view);
};

} // namespace lft
