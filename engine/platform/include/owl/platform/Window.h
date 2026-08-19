#pragma once

#include <memory>
#include <optional>

namespace owl::platform
{
class Platform;
class SDLWindowAccess;

struct FramebufferExtent
{
    int width = 0;
    int height = 0;
};

class Window
{
  public:
    Window() noexcept;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::optional<FramebufferExtent> GetFramebufferExtent() const noexcept;

  private:
    friend class Platform;
    friend class SDLWindowAccess;

    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};
} // namespace owl::platform
