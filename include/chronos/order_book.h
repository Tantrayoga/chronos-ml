#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "chronos/memory_pool.h"
#include "chronos/types.h"

namespace chronos {

template <size_t MaxPriceLevels, size_t MaxOrders>
class OrderBook {
public:
    struct PriceLevel {
        uint64_t price = 0;
        Side side = Side::Buy;
        uint32_t head_idx = kInvalidIndex;
        uint32_t tail_idx = kInvalidIndex;
    };

    // Active levels are grouped by side and sorted by price within each side.
    std::array<PriceLevel, MaxPriceLevels> price_levels{};
    size_t active_price_level_count = 0;
    FixedObjectPool<Order, MaxOrders> orders;

    uint32_t find_price_level(Side side, uint64_t price) const noexcept {
        const size_t index = lower_bound_level(side, price);
        if (index == active_price_level_count || price_levels[index].side != side ||
            price_levels[index].price != price) {
            return kInvalidIndex;
        }
        return static_cast<uint32_t>(index);
    }

    uint32_t insert_price_level(Side side, uint64_t price) noexcept {
        const size_t index = lower_bound_level(side, price);
        if (index < active_price_level_count && price_levels[index].side == side &&
            price_levels[index].price == price) {
            return static_cast<uint32_t>(index);
        }
        if (active_price_level_count == MaxPriceLevels) {
            return kInvalidIndex;
        }

        for (size_t shift_index = active_price_level_count; shift_index > index; --shift_index) {
            price_levels[shift_index] = price_levels[shift_index - 1];
        }
        price_levels[index] = PriceLevel{price, side, kInvalidIndex, kInvalidIndex};
        ++active_price_level_count;
        return static_cast<uint32_t>(index);
    }

    void append_order(uint32_t level_idx, uint32_t order_idx) noexcept {
        orders.get(order_idx).next_idx = kInvalidIndex;
        if(price_levels[level_idx].tail_idx == kInvalidIndex){
            orders.get(order_idx).prev_idx = kInvalidIndex;
            price_levels[level_idx].tail_idx = order_idx;
            price_levels[level_idx].head_idx = order_idx;
        }
        else{
            orders.get(price_levels[level_idx].tail_idx).next_idx = order_idx;
            orders.get(order_idx).prev_idx = price_levels[level_idx].tail_idx;
            price_levels[level_idx].tail_idx = order_idx;
        }
    }

    void unlink_order(uint32_t level_idx, uint32_t order_idx) noexcept {
        if(orders.get(order_idx).prev_idx != kInvalidIndex){
            orders.get(orders.get(order_idx).prev_idx).next_idx = orders.get(order_idx).next_idx;
        }
        else{
            price_levels[level_idx].head_idx = orders.get(order_idx).next_idx;
        }
        if(orders.get(order_idx).next_idx != kInvalidIndex){
            orders.get(orders.get(order_idx).next_idx).prev_idx = orders.get(order_idx).prev_idx;
        }
        else{
            price_levels[level_idx].tail_idx = orders.get(order_idx).prev_idx;
        }
    }

    // Places a resting/aggressive order: allocates a slot, tries to match it
    // against the opposite book immediately, then rests any remainder at its
    // limit price. Returns the order's pool index, or kInvalidIndex if the
    // pool or price-level table is exhausted.
    uint32_t add_order(uint64_t order_id, Side side, uint64_t price, uint32_t quantity) noexcept {
        const uint32_t order_idx = orders.allocate();
        if (order_idx == kInvalidIndex) {
            return kInvalidIndex;
        }

        Order& order = orders.get(order_idx);
        order.order_id = order_id;
        order.price = price;
        order.quantity = quantity;
        order.filled_qty = 0;
        order.next_idx = kInvalidIndex;
        order.prev_idx = kInvalidIndex;
        order.side = static_cast<char>(side);
        order.flags = static_cast<uint8_t>(OrderFlags::Active);

        match(order_idx);

        // Fully filled by match() — nothing left to rest on the book.
        if (order.filled_qty >= order.quantity) {
            orders.deallocate(order_idx);
            return order_idx;
        }

        const uint32_t level_idx = insert_price_level(side, price);
        if (level_idx == kInvalidIndex) {
            orders.deallocate(order_idx);
            return kInvalidIndex;
        }
        append_order(level_idx, order_idx);
        return order_idx;
    }

    // Removes a resting order from its price level's queue and returns its
    // slot to the pool. `level_idx` must be the level the order currently
    // sits on (callers already know it from routing, so we avoid a redundant
    // lookup here).
    void cancel_order(uint32_t level_idx, uint32_t order_idx) noexcept {
        unlink_order(level_idx, order_idx);
        orders.deallocate(order_idx);
    }

    // Convenience overload for callers that only have the order's id/side/price
    // (e.g. an external order-id -> index map) and not its level index handy.
    bool cancel_order(Side side, uint64_t price, uint32_t order_idx) noexcept {
        const uint32_t level_idx = find_price_level(side, price);
        if (level_idx == kInvalidIndex) {
            return false;
        }
        cancel_order(level_idx, order_idx);
        return true;
    }

    void match(uint32_t taker_idx) noexcept {
        Order& taker = orders.get(taker_idx);
        const Side taker_side = static_cast<Side>(taker.side);

        size_t sell_boundary = lower_bound_level(Side::Sell, 0);
        size_t current_level_idx;
        
        if(taker_side == Side::Buy){
            current_level_idx = sell_boundary;
        }
        else{
            if(sell_boundary == 0) return;
            current_level_idx = sell_boundary - 1;
        }
        
        while(taker.filled_qty < taker.quantity){
            if (current_level_idx >= active_price_level_count) break;

            PriceLevel& level = price_levels[current_level_idx];

            if(taker_side == Side::Buy && taker.price < level.price) break;
            if(taker_side == Side::Sell && taker.price > level.price) break;

            uint32_t curr_order_idx = level.head_idx;
            while (curr_order_idx != kInvalidIndex && taker.filled_qty < taker.quantity){
                Order& resting = orders.get(curr_order_idx);

                uint32_t next_idx = resting.next_idx;

                uint32_t taker_rem = taker.quantity - taker.filled_qty;
                uint32_t resting_rem = resting.quantity - resting.filled_qty;

                uint32_t trade_qty = std::min(taker_rem, resting_rem);
                taker.filled_qty += trade_qty;
                resting.filled_qty += trade_qty;

                if(resting.filled_qty == resting.quantity){
                    unlink_order(current_level_idx, curr_order_idx);
                    orders.deallocate(curr_order_idx);
                }

                curr_order_idx = next_idx;
            }

            // The inner loop above only unlinks *orders* from the level's
            // queue — it never touches the `price_levels` array itself. If
            // every resting order at this price got fully filled, head_idx
            // is now kInvalidIndex, but the (now-empty) PriceLevel slot is
            // still sitting in the array and `find_price_level` would still
            // "find" it. So: if the level is empty, erase its slot the same
            // way `insert_price_level` inserts one — shift everything above
            // it down by one and shrink the count.
            const bool level_emptied = (level.head_idx == kInvalidIndex);
            if (level_emptied) {
                remove_price_level(current_level_idx);
            }

            // Advancing the index has to account for the shift above.
            //   Buy taker walks forward (toward higher indices / the Sell
            //   block). If we just erased the level at current_level_idx,
            //   everything above it slid down into that same index — so the
            //   *next* level to check is already at current_level_idx, and
            //   incrementing would skip it. Only advance if we didn't erase.
            //   Sell taker walks backward (toward index 0 / the Buy block).
            //   A removal at current_level_idx never moves anything below
            //   it, so decrementing is always correct regardless of removal.
            if (taker_side == Side::Buy) {
                if (!level_emptied) {
                    ++current_level_idx;
                }
            } else {
                if (current_level_idx == 0) {
                    break; // would underflow — nothing lower to check anyway
                }
                --current_level_idx;
            }

        }

    }

private:
    // Erases the PriceLevel at `index`, compacting the array so no gap is
    // left — the mirror image of the shift-up in insert_price_level().
    void remove_price_level(size_t index) noexcept {
        for (size_t shift_index = index; shift_index + 1 < active_price_level_count; ++shift_index) {
            price_levels[shift_index] = price_levels[shift_index + 1];
        }
        --active_price_level_count;
    }

    size_t lower_bound_level(Side side, uint64_t price) const noexcept {
        size_t first = 0;
        size_t count = active_price_level_count;
        while (count > 0) {
            const size_t step = count / 2;
            const size_t index = first + step;
            const PriceLevel& level = price_levels[index];
            const char level_side = static_cast<char>(level.side);
            const char target_side = static_cast<char>(side);
            if (level_side < target_side ||
                (level_side == target_side && level.price < price)) {
                first = index + 1;
                count -= step + 1;
            } else {
                count = step;
            }
        }
        return first;
    }
};

} // namespace chronos