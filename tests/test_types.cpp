#include <catch2/catch_test_macros.hpp>
#include "chronos/types.h"

TEST_CASE("Order is tightly packed", "[types]") {
    REQUIRE(sizeof(chronos::Order) == 34);
}
