#include "order_book.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace lob {

std::string to_string(Side side) { return side == Side::Buy ? "BUY" : "SELL"; }

OrderResult OrderBook::submit(const OrderRequest& r) {
    if (r.id == 0 || r.quantity == 0) throw std::invalid_argument("ID and quantity must be positive");
    if (index_.contains(r.id)) throw std::invalid_argument("duplicate resting order ID");
    if (r.type == OrderType::Limit && r.limit_price <= 0)
        throw std::invalid_argument("limit price must be positive");

    const bool is_market = r.type == OrderType::Market;
    Quantity left;
    if (r.side == Side::Buy) {
        // A buy crosses any ask priced at or below its limit.
        left = match_against(asks_, r, [&](Price ask) { return is_market || ask <= r.limit_price; });
    } else {
        left = match_against(bids_, r, [&](Price bid) { return is_market || bid >= r.limit_price; });
    }

    OrderResult result{r.quantity - left, left, false};
    if (left && !is_market) {
        if (r.side == Side::Buy) rest_in(bids_, r.side, r.limit_price, r.id, left);
        else rest_in(asks_, r.side, r.limit_price, r.id, left);
        result.rested = true;
    }
    return result;
}

// Walk the opposite side from the best price outward, filling FIFO within each
// level, until the order is filled or the next level no longer crosses.
template <class Book, class Crosses>
Quantity OrderBook::match_against(Book& book, const OrderRequest& r, Crosses crosses) {
    Quantity left = r.quantity;
    while (left && !book.empty()) {
        auto level = book.begin();
        if (!crosses(level->first)) break;
        auto& lvl = level->second;
        while (left && !lvl.orders.empty()) {
            Resting& maker = lvl.orders.front();
            const Quantity qty = std::min(left, maker.remaining);
            trades_.push_back({maker.id, r.id, level->first, qty});
            maker.remaining -= qty;
            lvl.total -= qty;
            left -= qty;
            if (maker.remaining == 0) {
                index_.erase(maker.id);
                lvl.orders.pop_front();
            }
        }
        if (lvl.orders.empty()) book.erase(level);
    }
    return left;
}

template <class Book>
void OrderBook::rest_in(Book& book, Side side, Price price, OrderId id, Quantity qty) {
    auto& lvl = book[price];
    lvl.orders.push_back({id, qty});
    lvl.total += qty;
    index_.emplace(id, Location{side, price, std::prev(lvl.orders.end())});
}

template <class Book>
void OrderBook::remove_from(Book& book, const Location& loc) {
    auto level = book.find(loc.price);
    level->second.total -= loc.it->remaining;
    level->second.orders.erase(loc.it);
    if (level->second.orders.empty()) book.erase(level);
}

bool OrderBook::cancel(OrderId id) {
    auto found = index_.find(id);
    if (found == index_.end()) return false;
    if (found->second.side == Side::Buy) remove_from(bids_, found->second);
    else remove_from(asks_, found->second);
    index_.erase(found);
    return true;
}

bool OrderBook::reduce(OrderId id, Quantity new_quantity) {
    auto found = index_.find(id);
    if (found == index_.end()) return false;
    const Location& loc = found->second;
    if (new_quantity >= loc.it->remaining)
        throw std::invalid_argument("reduce must lower the quantity; cancel and resubmit to increase");
    if (new_quantity == 0) return cancel(id);
    const Quantity delta = loc.it->remaining - new_quantity;
    loc.it->remaining = new_quantity;
    if (loc.side == Side::Buy) bids_.find(loc.price)->second.total -= delta;
    else asks_.find(loc.price)->second.total -= delta;
    return true;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<Price> OrderBook::spread() const {
    if (bids_.empty() || asks_.empty()) return std::nullopt;
    return asks_.begin()->first - bids_.begin()->first;
}

std::vector<Level> OrderBook::depth(Side side, std::size_t count) const {
    std::vector<Level> out;
    auto append = [&](const auto& book) {
        for (const auto& [price, lvl] : book) {
            if (out.size() >= count) break;
            out.push_back({price, lvl.total, lvl.orders.size()});
        }
    };
    if (side == Side::Buy) append(bids_);
    else append(asks_);
    return out;
}

}  // namespace lob
