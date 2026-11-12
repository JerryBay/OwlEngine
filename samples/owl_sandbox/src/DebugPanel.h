#pragma once

#include <owl/platform/Platform.h>
#include <owl/vulkan/VulkanMemoryBudget.h>

#include <chrono>
#include <optional>
#include <string>

struct ImGuiContext;
union SDL_Event;

namespace owl::vulkan
{
    class VulkanTriangle;
}

namespace owl::sandbox
{
    // Sample-owned CPU UI. Keep this and the window alive until after renderer teardown.
    class DebugPanel
    {
    public:
        DebugPanel() = default;
        ~DebugPanel();
        DebugPanel(const DebugPanel&) = delete;
        DebugPanel& operator=(const DebugPanel&) = delete;

        [[nodiscard]] bool Initialize(owl::platform::Window& window, std::string& error);
        [[nodiscard]] bool Attach(owl::vulkan::VulkanTriangle& renderer, std::string& error);
        [[nodiscard]] owl::platform::EventPumpResult PumpEvents() noexcept;
        [[nodiscard]] bool WantsKeyboard() const noexcept;

    private:
        static void ObserveEvent(const SDL_Event& event, void* userdata) noexcept;
        static void BuildFrame(void* userdata);
        void Draw();

        ImGuiContext* context_ = nullptr;
        bool platformInitialized_ = false;
        owl::vulkan::VulkanTriangle* renderer_ = nullptr;
        std::optional<owl::vulkan::VulkanMemoryBudgetStats> budgets_;
        std::chrono::steady_clock::time_point nextBudgetRefresh_{};
    };
} // namespace owl::sandbox
