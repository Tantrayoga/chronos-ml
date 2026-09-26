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