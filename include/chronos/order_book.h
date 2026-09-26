#pragma once

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

private:
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