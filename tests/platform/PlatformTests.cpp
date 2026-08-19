#include <owl/platform/Platform.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<owl::platform::Platform>);
static_assert(!std::is_copy_assignable_v<owl::platform::Platform>);
static_assert(std::is_move_constructible_v<owl::platform::Platform>);
static_assert(std::is_move_assignable_v<owl::platform::Platform>);

TEST_CASE("Window descriptions default to no surface API and can request one", "[platform]")
{
    owl::platform::WindowDesc desc;
    CHECK(desc.surfaceApi == owl::platform::WindowSurfaceApi::None);

    desc.surfaceApi = owl::platform::WindowSurfaceApi::Vulkan;
    CHECK(desc.surfaceApi == owl::platform::WindowSurfaceApi::Vulkan);
}

TEST_CASE("Uninitialized platforms reject window creation", "[platform]")
{
    owl::platform::Platform platform;
    std::string error;

    const auto window = platform.CreateWindow({}, error);

    CHECK_FALSE(window.has_value());
    CHECK(error == "Platform is not initialized");
}
