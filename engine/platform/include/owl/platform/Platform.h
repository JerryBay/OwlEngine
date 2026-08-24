#pragma once

#include <owl/platform/Window.h>

#include <optional>
#include <string>

namespace owl::platform
{
    enum class WindowSurfaceApi
    {
        None,
        Vulkan,
    };

    struct WindowDesc
    {
        std::string title;
        int width = 1280;
        int height = 720;
        bool resizable = true;
        WindowSurfaceApi surfaceApi = WindowSurfaceApi::None;
    };

    enum class EventPumpResult
    {
        Continue,
        Quit,
    };

    class Platform
    {
    public:
        Platform() noexcept;
        ~Platform();

        Platform(const Platform&) = delete;
        Platform& operator=(const Platform&) = delete;

        Platform(Platform&& other) noexcept;
        Platform& operator=(Platform&& other) noexcept;

        [[nodiscard]] static std::optional<Platform> Create(std::string& error);

        [[nodiscard]] std::optional<Window> CreateWindow(const WindowDesc& desc,
                                                         std::string& error) const;

        [[nodiscard]] EventPumpResult PumpEvents() const noexcept;
        [[nodiscard]] std::string SdlVersion() const;
        [[nodiscard]] std::string VideoDriver() const;

    private:
        explicit Platform(bool initialized) noexcept;

        bool initialized_ = false;
    };
} // namespace owl::platform
