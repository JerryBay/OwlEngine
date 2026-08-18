#include <owl/platform/Platform.h>

#include <SDL3/SDL.h>

#include <string>
#include <utility>

namespace owl::platform
{
Platform::Platform() noexcept = default;

Platform::Platform(const bool initialized) noexcept
    : initialized_(initialized)
{
}

Platform::~Platform()
{
    if (initialized_)
    {
        SDL_Quit();
    }
}

Platform::Platform(Platform&& other) noexcept
    : initialized_(std::exchange(other.initialized_, false))
{
}

Platform& Platform::operator=(Platform&& other) noexcept
{
    if (this != &other)
    {
        if (initialized_)
        {
            SDL_Quit();
        }
        initialized_ = std::exchange(other.initialized_, false);
    }
    return *this;
}

std::optional<Platform> Platform::Create(std::string& error)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        error = std::string{"SDL_Init failed: "} + SDL_GetError();
        return std::nullopt;
    }

    error.clear();
    return Platform{true};
}

EventPumpResult Platform::PumpEvents() const noexcept
{
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT ||
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            return EventPumpResult::Quit;
        }

        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
        {
            return EventPumpResult::Quit;
        }
    }

    return EventPumpResult::Continue;
}

std::string Platform::SdlVersion() const
{
    return std::to_string(SDL_MAJOR_VERSION) + "." +
        std::to_string(SDL_MINOR_VERSION) + "." +
        std::to_string(SDL_MICRO_VERSION);
}

std::string Platform::VideoDriver() const
{
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver != nullptr ? std::string{driver} : std::string{"unknown"};
}
}
