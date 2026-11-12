#pragma once

#include <string>

struct ImGuiContext;

namespace owl::vulkan
{
    class VulkanTriangle;

    // Private sample integration. Context/userdata must stay alive and at stable addresses
    // until after renderer destruction. Builder completes ImGui::Render synchronously.
    class VulkanDebugUiAccess
    {
    public:
        using BuildFrame = void (*)(void* userdata);
        [[nodiscard]] static bool Attach(VulkanTriangle& renderer, ImGuiContext* context,
                                         BuildFrame builder, void* userdata, std::string& error);
    };
} // namespace owl::vulkan
