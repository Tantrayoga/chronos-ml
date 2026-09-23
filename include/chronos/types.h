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

#pragma pack(push, 1)
struct Order {
    uint64_t order_id;      // 8 bytes
    uint64_t price;         // 8 bytes, fixed-point: real_price * 10'000
    uint32_t quantity;      // 4 bytes
    uint32_t filled_qty;    // 4 bytes
    uint32_t next_idx;      // 4 bytes, intrusive doubly-linked list (price-level queue)
    uint32_t prev_idx;      // 4 bytes
    char side;               // 1 byte, 'B' or 'S'
    uint8_t flags;           // 1 byte, bit 0 = active
};
#pragma pack(pop)

static_assert(sizeof(Order) == 34, "Order must stay packed to 34 bytes");
static_assert(alignof(Order) == 1, "packed Order must not impose alignment");

} // namespace chronos
