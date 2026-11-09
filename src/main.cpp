#include "order_book.hpp"
#include <iostream>
int main() {
    lob::OrderBook book;
    book.submit({1, lob::Side::Sell, lob::OrderType::Limit, 10100, 10});
    book.submit({2, lob::Side::Sell, lob::OrderType::Limit, 10200, 15});
    auto r = book.submit({3, lob::Side::Buy, lob::OrderType::Limit, 10150, 12});
    std::cout << "BUY filled=" << r.filled << " remaining=" << r.remaining << "\nTrades:\n";
    for (const auto& t : book.trades())
        std::cout << "maker=" << t.maker_id << " taker=" << t.taker_id
                  << " price_ticks=" << t.price << " qty=" << t.quantity << '\n';
    std::cout << "Best bid=" << (book.best_bid() ? std::to_string(*book.best_bid()) : "none")
              << " best ask=" << (book.best_ask() ? std::to_string(*book.best_ask()) : "none") << '\n';
}
