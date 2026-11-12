#pragma once

union SDL_Event;

namespace owl::platform
{
    enum class EventPumpResult;

    // Private synchronous bridge. Observer/userdata are borrowed only for this pump.
    // It sees each event before the platform's quit/Esc handling; it cannot consume it.
    class SDLEventAccess
    {
    public:
        using Observer = void (*)(const SDL_Event& event, void* userdata) noexcept;
        [[nodiscard]] static EventPumpResult Pump(Observer observer, void* userdata) noexcept;
    };
} // namespace owl::platform
