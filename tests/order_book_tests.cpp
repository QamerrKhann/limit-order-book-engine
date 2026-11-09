// Self-contained test runner. CHECK is not `assert`, so it still runs in
// Release builds (where NDEBUG removes asserts).
#include "order_book.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lob;

static int g_failures = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::cerr << "  FAILED " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        }                                                                              \
    } while (0)

// Like CHECK, but stops the current test group so later lines cannot index
// past the end of a container that turned out smaller than expected.
#define REQUIRE(cond)              \
    do {                           \
        int before_ = g_failures;  \
        CHECK(cond);               \
        if (g_failures != before_) \
            return;                \
    } while (0)

template <class F>
static bool throws_invalid(F f) {
    try { f(); } catch (const std::invalid_argument&) { return true; }
    return false;
}

static void fifo_within_level() {
    OrderBook b;
    b.submit({1, Side::Sell, OrderType::Limit, 101, 10});
    b.submit({2, Side::Sell, OrderType::Limit, 101, 5});
    auto r = b.submit({3, Side::Buy, OrderType::Limit, 101, 12});
    CHECK(r.filled == 12 && r.remaining == 0 && !r.rested);
    REQUIRE(b.trades().size() == 2);
    CHECK(b.trades()[0].maker_id == 1 && b.trades()[0].quantity == 10);
    CHECK(b.trades()[1].maker_id == 2 && b.trades()[1].quantity == 2);
    REQUIRE(b.depth(Side::Sell).size() == 1);
    CHECK(b.depth(Side::Sell)[0].quantity == 3);
    CHECK(b.depth(Side::Sell)[0].order_count == 1);
}

static void price_priority_and_maker_price() {
    OrderBook b;
    b.submit({1, Side::Sell, OrderType::Limit, 103, 5});
    b.submit({2, Side::Sell, OrderType::Limit, 101, 5});  // better price, arrived later
    b.submit({3, Side::Sell, OrderType::Limit, 102, 5});
    b.submit({4, Side::Buy, OrderType::Limit, 105, 12});
    const auto& t = b.trades();
    REQUIRE(t.size() == 3);
    CHECK(t[0].maker_id == 2 && t[0].price == 101);  // executes at maker price, not 105
    CHECK(t[1].maker_id == 3 && t[1].price == 102);
    CHECK(t[2].maker_id == 1 && t[2].price == 103 && t[2].quantity == 2);
    CHECK(b.best_ask() == 103);
}

static void limit_rests_remainder_and_stops_at_limit() {
    OrderBook b;
    b.submit({1, Side::Sell, OrderType::Limit, 100, 4});
    b.submit({2, Side::Sell, OrderType::Limit, 110, 4});
    auto r = b.submit({3, Side::Buy, OrderType::Limit, 105, 10});
    CHECK(r.filled == 4 && r.remaining == 6 && r.rested);
    CHECK(b.best_bid() == 105 && b.best_ask() == 110 && b.spread() == 5);
    CHECK(b.resting_order_count() == 2);
}

static void market_order_never_rests() {
    OrderBook b;
    b.submit({10, Side::Buy, OrderType::Limit, 99, 7});
    auto r = b.submit({11, Side::Sell, OrderType::Market, 0, 10});
    CHECK(r.filled == 7 && r.remaining == 3 && !r.rested);
    CHECK(!b.best_bid() && !b.best_ask() && b.resting_order_count() == 0);
    auto empty = b.submit({12, Side::Buy, OrderType::Market, 0, 5});
    CHECK(empty.filled == 0 && empty.remaining == 5);
}

static void cancel_from_middle_keeps_fifo() {
    OrderBook b;
    b.submit({1, Side::Buy, OrderType::Limit, 100, 1});
    b.submit({2, Side::Buy, OrderType::Limit, 100, 2});
    b.submit({3, Side::Buy, OrderType::Limit, 100, 3});
    CHECK(b.cancel(2));
    CHECK(!b.cancel(2));
    REQUIRE(b.depth(Side::Buy).size() == 1);
    CHECK(b.depth(Side::Buy)[0].quantity == 4 && b.depth(Side::Buy)[0].order_count == 2);
    b.submit({4, Side::Sell, OrderType::Market, 0, 4});
    REQUIRE(b.trades().size() == 2);
    CHECK(b.trades()[0].maker_id == 1 && b.trades()[1].maker_id == 3);
    CHECK(!b.best_bid());
}

static void cancel_last_order_removes_level() {
    OrderBook b;
    b.submit({20, Side::Buy, OrderType::Limit, 100, 4});
    CHECK(b.cancel(20) && !b.best_bid() && b.depth(Side::Buy).empty());
}

static void reduce_keeps_priority() {
    OrderBook b;
    b.submit({1, Side::Sell, OrderType::Limit, 100, 10});
    b.submit({2, Side::Sell, OrderType::Limit, 100, 10});
    CHECK(b.reduce(1, 3));
    REQUIRE(b.depth(Side::Sell).size() == 1);
    CHECK(b.depth(Side::Sell)[0].quantity == 13);
    b.submit({3, Side::Buy, OrderType::Limit, 100, 3});
    REQUIRE(!b.trades().empty());
    CHECK(b.trades().back().maker_id == 1);  // still first in line
    CHECK(throws_invalid([&] { b.reduce(2, 50); }));
    CHECK(!b.reduce(999, 1));
}

static void partially_filled_maker_keeps_place() {
    OrderBook b;
    b.submit({1, Side::Buy, OrderType::Limit, 100, 10});
    b.submit({2, Side::Buy, OrderType::Limit, 100, 10});
    b.submit({3, Side::Sell, OrderType::Limit, 100, 4});
    b.submit({4, Side::Sell, OrderType::Limit, 100, 4});
    REQUIRE(b.trades().size() == 2 && b.depth(Side::Buy).size() == 1);
    CHECK(b.trades()[1].maker_id == 1);  // 1 still has 6 left and stays at the front
    CHECK(b.depth(Side::Buy)[0].quantity == 12);
}

static void rejects_bad_input() {
    OrderBook b;
    b.submit({30, Side::Buy, OrderType::Limit, 100, 4});
    CHECK(throws_invalid([&] { b.submit({30, Side::Buy, OrderType::Limit, 99, 1}); }));
    CHECK(throws_invalid([&] { b.submit({0, Side::Buy, OrderType::Limit, 99, 1}); }));
    CHECK(throws_invalid([&] { b.submit({31, Side::Buy, OrderType::Limit, 99, 0}); }));
    CHECK(throws_invalid([&] { b.submit({32, Side::Buy, OrderType::Limit, 0, 1}); }));
    CHECK(b.resting_order_count() == 1);  // rejected orders change nothing
}

// ---- Randomised differential test against a brute-force reference ----------
// The reference stores every resting order in one vector and finds the best
// match by scanning. It is obviously correct and obviously slow.
struct NaiveBook {
    struct O { OrderId id; Side side; Price px; Quantity qty; std::uint64_t seq; };
    std::vector<O> orders;
    std::vector<Trade> trades;
    std::uint64_t seq = 0;

    void submit(const OrderRequest& r) {
        Quantity left = r.quantity;
        while (left) {
            int best = -1;
            for (int i = 0; i < (int)orders.size(); ++i) {
                const O& o = orders[i];
                if (o.side == r.side) continue;
                bool crosses = r.type == OrderType::Market ||
                               (r.side == Side::Buy ? o.px <= r.limit_price : o.px >= r.limit_price);
                if (!crosses) continue;
                if (best < 0) { best = i; continue; }
                const O& b = orders[best];
                bool better = r.side == Side::Buy ? o.px < b.px : o.px > b.px;
                if (better || (o.px == b.px && o.seq < b.seq)) best = i;
            }
            if (best < 0) break;
            Quantity q = std::min(left, orders[best].qty);
            trades.push_back({orders[best].id, r.id, orders[best].px, q});
            orders[best].qty -= q;
            left -= q;
            if (!orders[best].qty) orders.erase(orders.begin() + best);
        }
        if (left && r.type == OrderType::Limit) orders.push_back({r.id, r.side, r.limit_price, left, seq++});
    }
    bool cancel(OrderId id) {
        auto it = std::find_if(orders.begin(), orders.end(), [&](const O& o) { return o.id == id; });
        if (it == orders.end()) return false;
        orders.erase(it);
        return true;
    }
    std::vector<Level> depth(Side side) const {
        std::map<Price, Level> agg;
        for (const O& o : orders) if (o.side == side) {
            auto& l = agg[o.px];
            l.price = o.px; l.quantity += o.qty; l.order_count++;
        }
        std::vector<Level> out;
        for (auto& [p, l] : agg) out.push_back(l);
        if (side == Side::Buy) std::reverse(out.begin(), out.end());
        return out;
    }
};

static bool same(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].price != b[i].price || a[i].quantity != b[i].quantity || a[i].order_count != b[i].order_count)
            return false;
    return true;
}

static void matches_reference_model() {
    for (unsigned seed = 1; seed <= 20; ++seed) {
        std::mt19937_64 rng(seed);
        OrderBook fast;
        NaiveBook ref;
        OrderId next_id = 1;
        std::vector<OrderId> ids;
        for (int step = 0; step < 2000; ++step) {
            int action = rng() % 10;
            if (action < 2 && !ids.empty()) {
                OrderId id = ids[rng() % ids.size()];
                CHECK(fast.cancel(id) == ref.cancel(id));
            } else {
                OrderRequest r{next_id++, rng() % 2 ? Side::Buy : Side::Sell,
                               action == 9 ? OrderType::Market : OrderType::Limit,
                               Price(95 + rng() % 11), Quantity(1 + rng() % 20)};
                fast.submit(r);
                ref.submit(r);
                ids.push_back(r.id);
            }
        }
        CHECK(fast.trades().size() == ref.trades.size());
        bool trades_equal = fast.trades().size() == ref.trades.size();
        for (std::size_t i = 0; trades_equal && i < ref.trades.size(); ++i) {
            const Trade &a = fast.trades()[i], &b = ref.trades[i];
            trades_equal = a.maker_id == b.maker_id && a.taker_id == b.taker_id &&
                           a.price == b.price && a.quantity == b.quantity;
        }
        CHECK(trades_equal);
        CHECK(same(fast.depth(Side::Buy, 1000), ref.depth(Side::Buy)));
        CHECK(same(fast.depth(Side::Sell, 1000), ref.depth(Side::Sell)));
        // A book at rest is never crossed.
        if (fast.best_bid() && fast.best_ask()) CHECK(*fast.best_bid() < *fast.best_ask());
    }
}

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests = {
        {"fifo_within_level", fifo_within_level},
        {"price_priority_and_maker_price", price_priority_and_maker_price},
        {"limit_rests_remainder_and_stops_at_limit", limit_rests_remainder_and_stops_at_limit},
        {"market_order_never_rests", market_order_never_rests},
        {"cancel_from_middle_keeps_fifo", cancel_from_middle_keeps_fifo},
        {"cancel_last_order_removes_level", cancel_last_order_removes_level},
        {"reduce_keeps_priority", reduce_keeps_priority},
        {"partially_filled_maker_keeps_place", partially_filled_maker_keeps_place},
        {"rejects_bad_input", rejects_bad_input},
        {"matches_reference_model (20 seeds x 2000 ops)", matches_reference_model},
    };
    for (auto& [name, fn] : tests) {
        int before = g_failures;
        fn();
        std::cout << (g_failures == before ? "[PASS] " : "[FAIL] ") << name << '\n';
    }
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All " << tests.size() << " test groups passed.\n";
}
