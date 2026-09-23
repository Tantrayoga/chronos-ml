#pragma once

#include <cstdint>

namespace chronos {

// Sentinel for "no node" in the intrusive index-linked list. Using a value
// (not a pointer) means Order stays trivially copyable and the whole book
// can live in one flat array — no heap, no pointer chasing across pages.
inline constexpr uint32_t kInvalidIndex = UINT32_MAX;

enum class Side : char {
    Buy = 'B',
    Sell = 'S',
};

enum class OrderFlags : uint8_t {
    None   = 0,
    Active = 1 << 0,
};

struct Order {
    uint64_t order_id;
    uint64_t price;        // fixed-point: real_price * 10'000
    uint32_t quantity;
    uint32_t filled_qty;
    uint32_t next_idx;     // intrusive doubly-linked list (price-level queue)
    uint32_t prev_idx;
    char side;             // 'B' or 'S'
    uint8_t flags;         // bit 0 = active
    uint8_t padding[6];    // pad to 8-byte alignment so Order[i] never straddles a cache line
};

static_assert(sizeof(Order) == 40, "Order must be exactly 40 bytes");
static_assert(alignof(Order) == 8, "Order must retain natural 8-byte alignment");

} // namespace chronos
