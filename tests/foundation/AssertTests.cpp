#include <owl/foundation/Assert.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Passing assertions continue execution", "[assertion]")
{
    OWL_ASSERT(true, "a passing assertion must not terminate");
    SUCCEED();
}
