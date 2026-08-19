#include "SDLWindowAccess.h"

#include <owl/foundation/Assert.h>
#include <owl/platform/Platform.h>
#include <owl/platform/Window.h>

#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace owl::platform
{
struct Window::Impl
{
    explicit Impl(SDL_Window* window) noexcept : window(window) {}

    ~Impl()
    {
        if (window != nullptr)
        {
            SDL_DestroyWindow(window);
        }
    }

    SDL_Window* window = nullptr;
};

Window::Window() noexcept = default;
Window::~Window() = default;
Window::Window(Window&& other) noexcept = default;
Window& Window::operator=(Window&& other) noexcept = default;

Window::Window(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

bool Window::IsValid() const noexcept
{
    return impl_ != nullptr && impl_->window != nullptr;
}

std::optional<FramebufferExtent> Window::GetFramebufferExtent() const noexcept
{
    if (!IsValid())
    {
        return std::nullopt;
    }

    FramebufferExtent extent;
    if (!SDL_GetWindowSizeInPixels(impl_->window, &extent.width, &extent.height))
    {
        return std::nullopt;
    }

    return extent;
}

SDL_Window* SDLWindowAccess::Get(Window& window) noexcept
{
    return window.impl_ != nullptr ? window.impl_->window : nullptr;
}

std::optional<Window> Platform::CreateWindow(const WindowDesc& desc, std::string& error) const
{
    if (!initialized_)
    {
        error = "Platform is not initialized";
        return std::nullopt;
    }

    OWL_ASSERT(desc.width > 0, "Window width must be positive");
    OWL_ASSERT(desc.height > 0, "Window height must be positive");

    SDL_WindowFlags flags = 0;
    if (desc.resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc.surfaceApi == WindowSurfaceApi::Vulkan)
    {
        flags |= SDL_WINDOW_VULKAN;
    }

    SDL_Window* handle = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);

    if (handle == nullptr)
    {
        error = std::string{"SDL_CreateWindow failed: "} + SDL_GetError();
        return std::nullopt;
    }

    error.clear();
    return Window{std::make_unique<Window::Impl>(handle)};
}
} // namespace owl::platform
