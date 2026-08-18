#pragma once

#include <memory>

namespace owl::platform
{
class Platform;

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

  private:
    friend class Platform;

    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};
} // namespace owl::platform
