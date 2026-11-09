# Limit Order Book Engine (C++20)

A single-threaded limit order book with price-time priority matching, written in C++20.

## Features
- Limit and market orders, partial fills, cancel, and reduce-quantity (which keeps the order's place in the queue)
- FIFO queues at each price level; every trade executes at the resting (maker) order's price
- Best bid/ask, spread, aggregated depth and a trade tape
- Prices stored as integer ticks, so there are no floating-point rounding errors
- Randomized differential test against a brute-force reference book
- Benchmark executable and an AddressSanitizer/UBSan build option

## Build, test, run
Requires CMake 3.20+ and a C++20 compiler (tested with GCC 13.3).
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/order_book_demo
./build/order_book_bench            # optional: ./build/order_book_bench 5000000

# Memory and undefined-behaviour checking:
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLOB_SANITIZE=ON
cmake --build build-asan && ./build-asan/order_book_tests
```

## Design
| Structure | Choice | Why |
|---|---|---|
| Price levels | `std::map<Price, PriceLevel>`, bids sorted descending, asks ascending | The best price is always `begin()`; inserting a new level costs O(log L) |
| Orders at a level | `std::list<Resting>` | FIFO order, and removing from the middle (a cancel) is O(1) |
| Cancel lookup | `unordered_map<OrderId, {side, price, list iterator}>` | A cancel finds its order directly, with no scan |
| Level size | cached `total` on each level | `depth()` is O(levels) instead of O(orders) |

The buy and sell matching paths share one template, `match_against`, so they can't drift apart.

| Operation | Cost (L = price levels, F = fills) |
|---|---|
| submit | O(log L + F) |
| cancel / reduce | O(log L) |
| best bid / ask | O(1) |
| depth(n) | O(n) |

## Testing
- **Unit tests** cover FIFO within a level, price priority across levels, executing at the maker's price, market orders never resting, cancelling from the middle of a queue, reduce keeping priority, and input validation.
- **Differential test:** 20 random seeds × 2,000 operations are run through both this book and a deliberately naive reference (a flat vector it scans each time). The trade tapes and final depth must match exactly, and the book must never end up crossed.
- The tests use their own `CHECK`/`REQUIRE` macros, not `assert`. `assert` is compiled out in Release builds, which would make tests pass without checking anything.
- I verified that the suite catches injected bugs, such as `<=` changed to `<` in the crossing check, FIFO reversed, and a missing quantity update on cancel.

## Benchmark (measured, with context)
`order_book_bench` runs 1,000,000 pre-generated operations: 70% limit orders within ±50 ticks of the mid price (about 1 in 8 of them crossing the spread) and 30% cancels.

| Machine | Compiler | Result |
|---|---|---|
| 2-vCPU Intel Xeon @ 2.10 GHz (cloud VM) | GCC 13.3, `-O3` | ~5.1M ops/s, ~195 ns/op mean |

Run it on your own machine before quoting a number. The result depends heavily on the hardware and the order mix, and this is an average, not tail latency.

## Limitations (deliberate scope)
- Single-threaded. Callers must serialize access.
- `std::map` and `std::list` allocate for every node, which hurts cache locality. A production engine would use an array indexed by price tick, or an intrusive list with a memory pool.
- No persistence, networking, auctions, fees, self-trade prevention, or IOC/FOK/stop order types.
- The trade tape grows without bound. A real system would stream trades to a consumer.

## Interview prep
**Why `std::list` and not `std::deque` or `std::vector`?** A list iterator stays valid while other elements are inserted or erased, so the index can store it and a cancel is O(1). With a vector, a cancel would need either an O(n) erase or lazy deletion (tombstones).

**Why does reduce keep priority but increasing size doesn't?** That matches exchange convention. Increasing size would let a trader jump ahead of others who joined the queue later, so an increase is treated as cancel-and-replace.

**Why integer ticks?** Floating-point values like 0.1 can't be represented exactly. Price comparisons and map keys must be exact.

**How would you make it faster?** Replace the map with a flat array indexed by price tick (O(1) level access when prices are dense), pool-allocate the order nodes, and avoid the `unordered_map` by using dense order IDs as indexes into a vector.

**How would you make it multi-threaded?** Don't share the book between threads. Give each instrument its own book on a single thread, and feed it through a lock-free single-producer/single-consumer queue. This is how real matching engines do it.

**How do you know it's correct?** The differential test against the naive reference, plus the sanitizer build, plus the injected bugs that the tests catch.
