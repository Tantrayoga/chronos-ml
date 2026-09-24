#include <catch2/catch_test_macros.hpp>
#include "chronos/memory_pool.h"

TEST_CASE("pool starts fully free", "[memory_pool]") {
    chronos::FixedObjectPool<int, 8> pool;
    REQUIRE(pool.free_count() == 8);
}

TEST_CASE("allocate returns distinct in-range indices", "[memory_pool]") {
    chronos::FixedObjectPool<int, 4> pool;

    uint32_t a = pool.allocate();
    uint32_t b = pool.allocate();
    uint32_t c = pool.allocate();

    REQUIRE(a != chronos::FixedObjectPool<int, 4>::kInvalidIndex);
    REQUIRE(a < 4);
    REQUIRE(b < 4);
    REQUIRE(c < 4);
    REQUIRE(a != b);
    REQUIRE(b != c);
    REQUIRE(a != c);
    REQUIRE(pool.free_count() == 1);
}

TEST_CASE("exhausted pool returns kInvalidIndex", "[memory_pool]") {
    chronos::FixedObjectPool<int, 2> pool;

    pool.allocate();
    pool.allocate();
    uint32_t overflow = pool.allocate();

    REQUIRE(overflow == chronos::FixedObjectPool<int, 2>::kInvalidIndex);
}

TEST_CASE("deallocate returns a slot for reuse", "[memory_pool]") {
    chronos::FixedObjectPool<int, 2> pool;

    uint32_t a = pool.allocate();
    pool.allocate();
    pool.deallocate(a);

    REQUIRE(pool.free_count() == 1);

    uint32_t reused = pool.allocate();
    REQUIRE(reused == a);
}

TEST_CASE("get() exposes the underlying storage slot", "[memory_pool]") {
    chronos::FixedObjectPool<int, 4> pool;

    uint32_t idx = pool.allocate();
    pool.get(idx) = 42;

    REQUIRE(pool.get(idx) == 42);
}
