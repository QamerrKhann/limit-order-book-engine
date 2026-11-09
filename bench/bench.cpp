// Throughput benchmark on a synthetic order flow.
// Numbers depend heavily on the machine, compiler and flow mix: report them
// with that context, never as a standalone claim.
#include "order_book.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    std::mt19937_64 rng(42);

    // Pre-generate the flow so the timed loop measures only the book.
    struct Op { bool cancel; lob::OrderRequest req; };
    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<lob::OrderId> live;
    lob::OrderId next_id = 1;
    for (std::size_t i = 0; i < n; ++i) {
        int roll = rng() % 100;
        if (roll < 30 && !live.empty()) {  // 30% cancels of a random earlier order
            std::size_t k = rng() % live.size();
            ops.push_back({true, {live[k], lob::Side::Buy, lob::OrderType::Limit, 0, 0}});
            live[k] = live.back();
            live.pop_back();
        } else {                            // 70% limit orders around a 10000-tick mid
            auto side = rng() % 2 ? lob::Side::Buy : lob::Side::Sell;
            lob::Price px = 10000 + (side == lob::Side::Buy ? -1 : 1) * lob::Price(rng() % 50) +
                            (rng() % 8 == 0 ? (side == lob::Side::Buy ? 20 : -20) : 0);  // ~1/8 aggressive
            ops.push_back({false, {next_id, side, lob::OrderType::Limit, px, 1 + rng() % 100}});
            live.push_back(next_id++);
        }
    }

    lob::OrderBook book;
    std::size_t cancels_hit = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const Op& op : ops) {
        if (op.cancel) cancels_hit += book.cancel(op.req.id);
        else book.submit(op.req);
    }
    auto t1 = std::chrono::steady_clock::now();

    double secs = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "operations:        " << n << '\n'
              << "elapsed:           " << secs * 1e3 << " ms\n"
              << "throughput:        " << n / secs / 1e6 << " M ops/s\n"
              << "mean latency:      " << secs / n * 1e9 << " ns/op\n"
              << "trades generated:  " << book.trades().size() << '\n'
              << "cancels that hit:  " << cancels_hit << '\n'
              << "resting at end:    " << book.resting_order_count() << '\n';
}
