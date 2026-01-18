#pragma once

#include <expected>
#include <optional>
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
    std::string timestamp; // ISO 8601 (lexicographically comparable)
};

struct Snapshot {
    std::string symbol;
    double latest_trade_price{};
    double latest_quote_bid{};
    double latest_quote_ask{};
    double prev_daily_bar_close{};
    std::string latest_trade_timestamp; // ISO 8601 (lexicographically comparable)
    long minute_bar_volume{};           // Volume from current minute bar

    // Market quality metrics (calculated from bid/ask)
    double spread_bps{};  // Spread in basis points
    bool tradeable{true}; // Whether spread/volume are acceptable
};

struct Bar {
    std::string timestamp; // ISO 8601 (lexicographically comparable)
    double open{};
    double high{};
    double low{};
    double close{};
    long volume{};
};

struct Position {
    std::string symbol;
    double qty{};
    double avg_entry_price{};
    double current_price{};
    double unrealized_pl{};
    double unrealized_plpc{};
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

    // Get latest quote for a single stock symbol (convenience wrapper)
    std::optional<Snapshot> get_snapshot(std::string_view);

    // Get latest quotes for crypto symbols
    std::expected<std::map<std::string, Snapshot>, AlpacaError>
    get_crypto_snapshots(const std::vector<std::string>&);

    // Get account information
    std::expected<std::string, AlpacaError> get_account();

    // Get all open positions (returns empty vector if no positions)
    std::vector<Position> get_positions();

    // Get all open orders (pending, new, accepted, partially_filled)
    std::expected<std::string, AlpacaError> get_open_orders();

    // Get all orders (open, closed, all statuses) for restart recovery
    std::expected<std::string, AlpacaError> get_all_orders();

    // Place a market order by notional amount (dollar-based, for stocks)
    std::expected<std::string, AlpacaError> place_order(std::string_view, std::string_view, double, std::string_view = "");

    // Place a market order by quantity (for crypto to avoid notional/qty confusion)
    std::expected<std::string, AlpacaError> place_order_qty(std::string_view, std::string_view, double, std::string_view = "");

    // Close a position by symbol
    std::expected<std::string, AlpacaError> close_position(std::string_view);

    // Get historic bars (timeframe: "1Min", "1Hour", "1Day", etc.) with start/end dates
    std::expected<std::vector<Bar>, AlpacaError> get_bars(std::string_view, std::string_view, std::string_view, std::string_view);

    // Get historic bars for last N days (convenience wrapper)
    std::optional<std::vector<Bar>> get_bars(std::string_view, std::string_view, int);

    // Get historic crypto bars
    std::expected<std::vector<Bar>, AlpacaError> get_crypto_bars(std::string_view, std::string_view, std::string_view, std::string_view);

private:
    std::string api_key_;
    std::string api_secret_;
    std::string base_url_;
    std::string data_url_;
    std::string data_api_key_;
    std::string data_api_secret_;

    std::string get_env_or_default(std::string_view, std::string_view);
};

} // namespace lft
