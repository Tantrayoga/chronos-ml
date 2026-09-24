#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace chronos {

// Fixed-capacity object pool. All storage is allocated once, inline, at
// construction. No malloc/free ever occurs after that point — acquiring and
// releasing slots is pure index bookkeeping over arrays that already exist.
template <typename T, size_t Capacity>
class FixedObjectPool {
public:
    static constexpr uint32_t kInvalidIndex = UINT32_MAX;

    FixedObjectPool() noexcept {
        for (uint32_t i = 0; i < Capacity; ++i) {
            free_indices_[i] = i;
        }
        free_count_ = Capacity;
    }

    FixedObjectPool(const FixedObjectPool&) = delete;
    FixedObjectPool& operator=(const FixedObjectPool&) = delete;

    // Returns the index of a free slot, or kInvalidIndex if the pool is
    // exhausted.
    uint32_t allocate() noexcept {
        if (free_count_ == 0) return kInvalidIndex;
        return free_indices_[--free_count_];
    }

    // Returns `index` to the free-list so a future allocate() can reuse it.
    void deallocate(uint32_t index) noexcept {
        free_indices_[free_count_++] = index;
    }

    T& get(uint32_t index) noexcept { return storage_[index]; }
    const T& get(uint32_t index) const noexcept { return storage_[index]; }

    size_t capacity() const noexcept { return Capacity; }
    size_t free_count() const noexcept { return free_count_; }

private:
    std::array<T, Capacity> storage_{};

    // Stack of currently-available slot indices. free_indices_[0 .. free_count_)
    // holds the valid entries; the rest is unused/stale.
    std::array<uint32_t, Capacity> free_indices_{};
    size_t free_count_ = 0;
};

} // namespace chronos
