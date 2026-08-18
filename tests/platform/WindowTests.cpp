#include <owl/platform/Window.h>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<owl::platform::Window>);
static_assert(!std::is_copy_assignable_v<owl::platform::Window>);
static_assert(std::is_move_constructible_v<owl::platform::Window>);
static_assert(std::is_move_assignable_v<owl::platform::Window>);

TEST_CASE("Default and moved empty windows remain invalid", "[window]")
{
    owl::platform::Window source;
    CHECK_FALSE(source.IsValid());

    owl::platform::Window destination{std::move(source)};
    CHECK_FALSE(source.IsValid());
    CHECK_FALSE(destination.IsValid());
}
