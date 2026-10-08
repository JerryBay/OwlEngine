#include <owl/platform/Platform.h>

#include <SDL3/SDL.h>

#include <string>
#include <utility>

namespace owl::platform
{
    Platform::Platform() noexcept = default;

    Platform::Platform(const bool initialized) noexcept : initialized_(initialized) {}

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

    std::optional<std::filesystem::path> Platform::ExecutableDirectory(std::string& error)
    {
        const char* basePath = SDL_GetBasePath();
        if (basePath == nullptr)
        {
            error = std::string{"SDL_GetBasePath failed: "} + SDL_GetError();
            return std::nullopt;
        }

        // SDL returns UTF-8. Preserve it when constructing a native Windows filesystem path.
        const std::u8string utf8Path{basePath, basePath + std::char_traits<char>::length(basePath)};
        error.clear();
        return std::filesystem::path{utf8Path};
    }

    EventPumpResult Platform::PumpEvents() const noexcept
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
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

    bool Platform::IsKeyDown(const Key key) const noexcept
    {
        if (!initialized_)
        {
            return false;
        }

        SDL_Scancode scancode;
        switch (key)
        {
        case Key::Digit0:
            scancode = SDL_SCANCODE_0;
            break;
        case Key::Digit1:
            scancode = SDL_SCANCODE_1;
            break;
        case Key::Digit2:
            scancode = SDL_SCANCODE_2;
            break;
        case Key::Digit3:
            scancode = SDL_SCANCODE_3;
            break;
        case Key::Digit4:
            scancode = SDL_SCANCODE_4;
            break;
        case Key::Digit5:
            scancode = SDL_SCANCODE_5;
            break;
        case Key::Digit6:
            scancode = SDL_SCANCODE_6;
            break;
        case Key::A:
            scancode = SDL_SCANCODE_A;
            break;
        case Key::Space:
            scancode = SDL_SCANCODE_SPACE;
            break;
        default:
            return false;
        }

        int keyCount = 0;
        const bool* state = SDL_GetKeyboardState(&keyCount);
        return state != nullptr && static_cast<int>(scancode) < keyCount && state[scancode];
    }

    std::string Platform::SdlVersion() const
    {
        return std::to_string(SDL_MAJOR_VERSION) + "." + std::to_string(SDL_MINOR_VERSION) + "." +
               std::to_string(SDL_MICRO_VERSION);
    }

    std::string Platform::VideoDriver() const
    {
        const char* driver = SDL_GetCurrentVideoDriver();
        return driver != nullptr ? std::string{driver} : std::string{"unknown"};
    }
} // namespace owl::platform
