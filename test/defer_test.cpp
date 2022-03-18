#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include "Defer.hpp"

void ChangeNumber(int& val, int to) {
    DEFER([&]() { val = to; });
}

TEST_CASE("defer", "[lambda]") {
    int a = 50;
    ChangeNumber(a, 100);
    REQUIRE(a == 100);
}
