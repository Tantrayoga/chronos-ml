#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "chronos/binary_parser.h"
#include "chronos/order_book.h"

TEST_CASE("parse_wire_order swaps big-endian wire bytes into host-endian fields", "[binary_parser]") {
    const std::array<uint8_t, chronos::kWireOrderSize> wire_bytes = {
        0x01,                                             // message_type: NewOrder
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,    // order_id (big-endian)
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,    // price (big-endian)
        0xAA, 0xBB, 0xCC, 0xDD,                            // quantity (big-endian)
        0x01,                                              // side: Sell
    };

    chronos::WireOrder parsed{};
    REQUIRE(chronos::parse_wire_order(wire_bytes.data(), wire_bytes.size(), parsed));

    REQUIRE(parsed.message_type == chronos::WireMessageType::NewOrder);
    REQUIRE(parsed.order_id == 0x0102030405060708ULL);
    REQUIRE(parsed.price == 0x1122334455667788ULL);
    REQUIRE(parsed.quantity == 0xAABBCCDDU);
    REQUIRE(parsed.side == chronos::WireSide::Sell);
}

TEST_CASE("parse_wire_order rejects a truncated buffer", "[binary_parser]") {
    const std::array<uint8_t, 10> short_buffer{};
    chronos::WireOrder parsed{};
    REQUIRE_FALSE(chronos::parse_wire_order(short_buffer.data(), short_buffer.size(), parsed));
}

TEST_CASE("parse_wire_order rejects a null buffer", "[binary_parser]") {
    chronos::WireOrder parsed{};
    REQUIRE_FALSE(chronos::parse_wire_order(nullptr, 0, parsed));
}

TEST_CASE("a parsed wire message routes directly into the OrderBook", "[binary_parser][integration]") {
    const std::array<uint8_t, chronos::kWireOrderSize> wire_bytes = {
        0x01,                                             // NewOrder
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2A,    // order_id = 42
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64,    // price = 100
        0x00, 0x00, 0x00, 0x0A,                            // quantity = 10
        0x00,                                              // side: Buy
    };

    chronos::WireOrder parsed{};
    REQUIRE(chronos::parse_wire_order(wire_bytes.data(), wire_bytes.size(), parsed));

    chronos::OrderBook<16, 16> book;
    const chronos::Side side = (parsed.side == chronos::WireSide::Buy) ? chronos::Side::Buy : chronos::Side::Sell;
    const uint32_t order_idx =
        book.add_order(parsed.order_id, side, parsed.price, static_cast<uint32_t>(parsed.quantity));

    REQUIRE(order_idx != chronos::kInvalidIndex);
    REQUIRE(book.orders.get(order_idx).order_id == 42);
    REQUIRE(book.orders.get(order_idx).price == 100);
    REQUIRE(book.orders.get(order_idx).quantity == 10);
}
