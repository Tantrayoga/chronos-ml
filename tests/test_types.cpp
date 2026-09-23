#include <catch2/catch_test_macros.hpp>
#include "chronos/types.h"

TEST_CASE("Order is cache-aligned", "[types]") {
    REQUIRE(sizeof(chronos::Order) == 40);
    REQUIRE(alignof(chronos::Order) == 8);
}
