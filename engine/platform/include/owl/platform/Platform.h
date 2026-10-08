#pragma once

#include <owl/platform/Window.h>

#include <filesystem>
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

    enum class Key
    {
        Digit0,
        Digit1,
        Digit2,
        Digit3,
        Digit4,
        Digit5,
        Digit6,
        A,
        Space,
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

        // Desktop executable base path for application-local assets; no video initialization
        // needed.
        [[nodiscard]] static std::optional<std::filesystem::path>
        ExecutableDirectory(std::string& error);

        [[nodiscard]] std::optional<Window> CreateWindow(const WindowDesc& desc,
                                                         std::string& error) const;

        [[nodiscard]] EventPumpResult PumpEvents() const noexcept;
        // Keyboard state is refreshed by PumpEvents. Call on the window thread.
        [[nodiscard]] bool IsKeyDown(Key key) const noexcept;
        [[nodiscard]] std::string SdlVersion() const;
        [[nodiscard]] std::string VideoDriver() const;

    private:
        explicit Platform(bool initialized) noexcept;

        bool initialized_ = false;
    };
} // namespace owl::platform
