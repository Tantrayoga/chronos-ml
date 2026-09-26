#include <array>

#include <catch2/catch_test_macros.hpp>

#include "chronos/order_book.h"

namespace {

using TestOrderBook = chronos::OrderBook<4, 8>;

uint32_t allocate_order(TestOrderBook& book) {
    const uint32_t index = book.orders.allocate();
    if (index != chronos::kInvalidIndex) {
        book.orders.get(index).prev_idx = chronos::kInvalidIndex;
        book.orders.get(index).next_idx = chronos::kInvalidIndex;
    }
    return index;
}

} // namespace

TEST_CASE("appending orders preserves FIFO links", "[order_book][queue]") {
    TestOrderBook book;
    const uint32_t level_idx = book.insert_price_level(chronos::Side::Buy, 100);
    const uint32_t first = allocate_order(book);
    const uint32_t second = allocate_order(book);

    REQUIRE(level_idx != chronos::kInvalidIndex);
    REQUIRE(first != chronos::kInvalidIndex);
    REQUIRE(second != chronos::kInvalidIndex);

    book.append_order(level_idx, first);
    book.append_order(level_idx, second);

    REQUIRE(book.price_levels[level_idx].head_idx == first);
    REQUIRE(book.price_levels[level_idx].tail_idx == second);
    REQUIRE(book.orders.get(first).prev_idx == chronos::kInvalidIndex);
    REQUIRE(book.orders.get(first).next_idx == second);
    REQUIRE(book.orders.get(second).prev_idx == first);
    REQUIRE(book.orders.get(second).next_idx == chronos::kInvalidIndex);
}

TEST_CASE("unlinking head, middle, tail, and final order repairs links", "[order_book][queue]") {
    TestOrderBook book;
    const uint32_t level_idx = book.insert_price_level(chronos::Side::Buy, 100);
    std::array<uint32_t, 5> order_indices{};

    REQUIRE(level_idx != chronos::kInvalidIndex);
    for (uint32_t& order_idx : order_indices) {
        order_idx = allocate_order(book);
        REQUIRE(order_idx != chronos::kInvalidIndex);
        book.append_order(level_idx, order_idx);
    }

    book.unlink_order(level_idx, order_indices[0]);
    REQUIRE(book.price_levels[level_idx].head_idx == order_indices[1]);
    REQUIRE(book.orders.get(order_indices[1]).prev_idx == chronos::kInvalidIndex);

    book.unlink_order(level_idx, order_indices[2]);
    REQUIRE(book.orders.get(order_indices[1]).next_idx == order_indices[3]);
    REQUIRE(book.orders.get(order_indices[3]).prev_idx == order_indices[1]);

    book.unlink_order(level_idx, order_indices[4]);
    REQUIRE(book.price_levels[level_idx].tail_idx == order_indices[3]);
    REQUIRE(book.orders.get(order_indices[3]).next_idx == chronos::kInvalidIndex);

    book.unlink_order(level_idx, order_indices[1]);
    REQUIRE(book.price_levels[level_idx].head_idx == order_indices[3]);
    REQUIRE(book.price_levels[level_idx].tail_idx == order_indices[3]);

    book.unlink_order(level_idx, order_indices[3]);
    REQUIRE(book.price_levels[level_idx].head_idx == chronos::kInvalidIndex);
    REQUIRE(book.price_levels[level_idx].tail_idx == chronos::kInvalidIndex);
}

TEST_CASE("price levels remain sorted and lookup handles duplicates and capacity", "[order_book][levels]") {
    chronos::OrderBook<4, 4> book;

    REQUIRE(book.find_price_level(chronos::Side::Buy, 20) == chronos::kInvalidIndex);
    const uint32_t level_30 = book.insert_price_level(chronos::Side::Buy, 30);
    REQUIRE(level_30 == 0);
    const uint32_t order_idx = book.orders.allocate();
    REQUIRE(order_idx != chronos::kInvalidIndex);
    book.append_order(level_30, order_idx);

    REQUIRE(book.insert_price_level(chronos::Side::Buy, 10) == 0);
    REQUIRE(book.find_price_level(chronos::Side::Buy, 30) == 1);
    REQUIRE(book.price_levels[1].head_idx == order_idx);
    REQUIRE(book.insert_price_level(chronos::Side::Buy, 20) == 1);
    REQUIRE(book.insert_price_level(chronos::Side::Buy, 20) == 1);
    REQUIRE(book.find_price_level(chronos::Side::Buy, 10) == 0);
    REQUIRE(book.find_price_level(chronos::Side::Buy, 20) == 1);
    REQUIRE(book.find_price_level(chronos::Side::Buy, 30) == 2);
    REQUIRE(book.find_price_level(chronos::Side::Buy, 25) == chronos::kInvalidIndex);
    REQUIRE(book.active_price_level_count == 3);

    const uint32_t sell_level = book.insert_price_level(chronos::Side::Sell, 20);
    REQUIRE(sell_level == 3);
    REQUIRE(book.find_price_level(chronos::Side::Sell, 20) == sell_level);
    REQUIRE(book.find_price_level(chronos::Side::Buy, 20) != sell_level);
    REQUIRE(book.active_price_level_count == 4);
    REQUIRE(book.insert_price_level(chronos::Side::Buy, 40) == chronos::kInvalidIndex);
}

TEST_CASE("market makers post resting bids and asks without crossing", "[order_book][matching]") {
    chronos::OrderBook<8, 8> book;

    const uint32_t bid_idx = book.add_order(1, chronos::Side::Buy, 100, 10);
    const uint32_t ask_idx = book.add_order(2, chronos::Side::Sell, 105, 10);

    REQUIRE(bid_idx != chronos::kInvalidIndex);
    REQUIRE(ask_idx != chronos::kInvalidIndex);
    REQUIRE(book.orders.get(bid_idx).filled_qty == 0);
    REQUIRE(book.orders.get(ask_idx).filled_qty == 0);

    const uint32_t bid_level = book.find_price_level(chronos::Side::Buy, 100);
    const uint32_t ask_level = book.find_price_level(chronos::Side::Sell, 105);
    REQUIRE(book.price_levels[bid_level].head_idx == bid_idx);
    REQUIRE(book.price_levels[ask_level].head_idx == ask_idx);
}

TEST_CASE("aggressive taker fully fills a single resting order at the best price", "[order_book][matching]") {
    chronos::OrderBook<8, 8> book;

    const uint32_t maker_idx = book.add_order(1, chronos::Side::Sell, 100, 10);
    REQUIRE(maker_idx != chronos::kInvalidIndex);

    const uint32_t taker_idx = book.add_order(2, chronos::Side::Buy, 100, 10);
    REQUIRE(taker_idx != chronos::kInvalidIndex);

    // Taker fully filled: add_order() deallocates it, so its slot is free again.
    REQUIRE(book.orders.free_count() == book.orders.capacity());
    REQUIRE(book.find_price_level(chronos::Side::Sell, 100) == chronos::kInvalidIndex);
}

TEST_CASE("taker partially fills a resting order and rests the remainder", "[order_book][matching]") {
    chronos::OrderBook<8, 8> book;

    const uint32_t maker_idx = book.add_order(1, chronos::Side::Sell, 100, 10);
    REQUIRE(maker_idx != chronos::kInvalidIndex);

    const uint32_t taker_idx = book.add_order(2, chronos::Side::Buy, 100, 4);
    REQUIRE(taker_idx != chronos::kInvalidIndex);

    REQUIRE(book.orders.get(maker_idx).filled_qty == 4);
    const uint32_t ask_level = book.find_price_level(chronos::Side::Sell, 100);
    REQUIRE(ask_level != chronos::kInvalidIndex);
    REQUIRE(book.price_levels[ask_level].head_idx == maker_idx);
}

TEST_CASE("taker sweeps multiple price levels and resting orders within a level", "[order_book][matching]") {
    chronos::OrderBook<8, 16> book;

    // Two makers resting FIFO at the same best price, one maker deeper in the book.
    const uint32_t maker_a = book.add_order(1, chronos::Side::Sell, 100, 5);
    const uint32_t maker_b = book.add_order(2, chronos::Side::Sell, 100, 5);
    const uint32_t maker_c = book.add_order(3, chronos::Side::Sell, 101, 5);
    REQUIRE(maker_a != chronos::kInvalidIndex);
    REQUIRE(maker_b != chronos::kInvalidIndex);
    REQUIRE(maker_c != chronos::kInvalidIndex);

    // Aggressive buy sweeps through both price levels: fills maker_a fully,
    // maker_b fully, and maker_c partially.
    const uint32_t taker_idx = book.add_order(4, chronos::Side::Buy, 101, 12);
    REQUIRE(taker_idx != chronos::kInvalidIndex);

    REQUIRE(book.find_price_level(chronos::Side::Sell, 100) == chronos::kInvalidIndex);
    const uint32_t level_101 = book.find_price_level(chronos::Side::Sell, 101);
    REQUIRE(level_101 != chronos::kInvalidIndex);
    REQUIRE(book.orders.get(maker_c).filled_qty == 2);
    REQUIRE(book.price_levels[level_101].head_idx == maker_c);
}

TEST_CASE("taker with insufficient limit price does not cross the book", "[order_book][matching]") {
    chronos::OrderBook<8, 8> book;

    const uint32_t maker_idx = book.add_order(1, chronos::Side::Sell, 100, 10);
    REQUIRE(maker_idx != chronos::kInvalidIndex);

    // Bid below the ask — should rest, not match.
    const uint32_t taker_idx = book.add_order(2, chronos::Side::Buy, 99, 10);
    REQUIRE(taker_idx != chronos::kInvalidIndex);

    REQUIRE(book.orders.get(maker_idx).filled_qty == 0);
    REQUIRE(book.orders.get(taker_idx).filled_qty == 0);
    REQUIRE(book.find_price_level(chronos::Side::Buy, 99) != chronos::kInvalidIndex);
    REQUIRE(book.find_price_level(chronos::Side::Sell, 100) != chronos::kInvalidIndex);
}