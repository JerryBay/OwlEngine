#pragma once

struct SDL_Window;

namespace owl::platform
{
class Window;

class SDLWindowAccess
{
  public:
    [[nodiscard]] static SDL_Window* Get(Window& window) noexcept;
};
} // namespace owl::platform
