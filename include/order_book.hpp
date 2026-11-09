#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lob {

using OrderId = std::uint64_t;
using Price = std::int64_t;     // Integer ticks, e.g. 10125 = $101.25. No floating point.
using Quantity = std::uint64_t;

enum class Side { Buy, Sell };
enum class OrderType { Limit, Market };

struct OrderRequest {
    OrderId id;
    Side side;
    OrderType type;
    Price limit_price;  // Ignored for market orders.
    Quantity quantity;
};

struct Trade {
    OrderId maker_id;   // Resting order that provided liquidity.
    OrderId taker_id;   // Incoming order that removed it.
    Price price;        // Always the maker's price.
    Quantity quantity;
};

struct Level {
    Price price;
    Quantity quantity;
    std::size_t order_count;
};

struct OrderResult {
    Quantity filled = 0;
    Quantity remaining = 0;  // For a limit order this rests; for a market order it is dropped.
    bool rested = false;
};

// Single-threaded limit order book with price-time priority.
//
// Complexity (L = number of price levels on a side, F = number of fills):
//   submit          O(log L + F)   (O(1) to join an existing level's queue)
//   cancel / reduce O(log L)       via an id -> list-iterator index; no scanning
//   best_bid/ask    O(1)
//   depth(n)        O(n)           each level caches its total quantity
class OrderBook {
public:
    OrderResult submit(const OrderRequest& request);
    bool cancel(OrderId id);
    // Lower a resting order's quantity in place. Keeps time priority
    // (increasing size would not, so it is rejected). Returns false if unknown.
    bool reduce(OrderId id, Quantity new_quantity);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    std::optional<Price> spread() const;
    std::vector<Level> depth(Side side, std::size_t count = 5) const;
    const std::vector<Trade>& trades() const noexcept { return trades_; }
    std::size_t resting_order_count() const noexcept { return index_.size(); }

private:
    struct Resting { OrderId id; Quantity remaining; };
    using Queue = std::list<Resting>;  // Stable iterators: erase from the middle is O(1).
    struct PriceLevel { Queue orders; Quantity total = 0; };
    using Bids = std::map<Price, PriceLevel, std::greater<Price>>;  // best (highest) first
    using Asks = std::map<Price, PriceLevel>;                       // best (lowest) first
    struct Location { Side side; Price price; Queue::iterator it; };

    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, Location> index_;
    std::vector<Trade> trades_;

    template <class Book, class Crosses>
    Quantity match_against(Book& book, const OrderRequest& r, Crosses crosses);
    template <class Book>
    void rest_in(Book& book, Side side, Price price, OrderId id, Quantity qty);
    template <class Book>
    void remove_from(Book& book, const Location& loc);
};

std::string to_string(Side side);

}  // namespace lob
