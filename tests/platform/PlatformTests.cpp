#include <owl/platform/Platform.h>

#include "SDLEventAccess.h"
#include <SDL3/SDL.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<owl::platform::Platform>);
static_assert(!std::is_copy_assignable_v<owl::platform::Platform>);
static_assert(std::is_move_constructible_v<owl::platform::Platform>);
static_assert(std::is_move_assignable_v<owl::platform::Platform>);

TEST_CASE("SDL event observers see ordered events before platform exit handling",
          "[platform][events]")
{
    REQUIRE(SDL_Init(SDL_INIT_EVENTS));
    struct EventScope
    {
        ~EventScope()
        {
            SDL_Quit();
        }
    } scope;
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    struct Observed
    {
        int users = 0;
        int escapes = 0;
    } observed;
    const auto observer = [](const SDL_Event& event, void* userdata) noexcept
    {
        auto& seen = *static_cast<Observed*>(userdata);
        if (event.type == SDL_EVENT_USER)
            ++seen.users;
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
            ++seen.escapes;
    };
    SDL_Event event{};
    event.type = SDL_EVENT_USER;
    REQUIRE(SDL_PushEvent(&event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_ESCAPE;
    REQUIRE(SDL_PushEvent(&event));
    CHECK(owl::platform::SDLEventAccess::Pump(observer, &observed) ==
          owl::platform::EventPumpResult::Quit);
    CHECK(observed.users == 1);
    CHECK(observed.escapes == 1);
    // Escape exits mid-pump; discard SDL's remaining poll sentinel for this separate check.
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    event = {};
    event.type = SDL_EVENT_QUIT;
    REQUIRE(SDL_PushEvent(&event));
    owl::platform::Platform platform;
    CHECK(platform.PumpEvents() == owl::platform::EventPumpResult::Quit);
}

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

TEST_CASE("Uninitialized platforms report no pressed keys", "[platform]")
{
    owl::platform::Platform platform;
    CHECK_FALSE(platform.IsKeyDown(owl::platform::Key::Digit0));
    CHECK_FALSE(platform.IsKeyDown(owl::platform::Key::A));
    CHECK_FALSE(platform.IsKeyDown(owl::platform::Key::Space));
}
