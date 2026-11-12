#pragma once

#include "VulkanDebugUiAccess.h"

#include <vulkan/vulkan.h>

namespace owl::vulkan
{
    class VulkanDevice;

    // Owns only the GPU backend and descriptor pool. The renderer drains GPU work before
    // destruction; the sample owns the context/platform backend and destroys them afterward.
    class VulkanImGuiRenderer
    {
    public:
        VulkanImGuiRenderer() = default;
        ~VulkanImGuiRenderer();
        VulkanImGuiRenderer(const VulkanImGuiRenderer&) = delete;
        VulkanImGuiRenderer& operator=(const VulkanImGuiRenderer&) = delete;

        [[nodiscard]] bool Attach(ImGuiContext* context, VulkanDebugUiAccess::BuildFrame builder,
                                  void* userdata, std::string& error);
        [[nodiscard]] bool IsAttached() const noexcept;
        // Called after the renderer's drain, also on swapchain format changes.
        [[nodiscard]] VkResult SetColorFormat(VkInstance instance, const VulkanDevice& device,
                                              VkFormat format, std::uint32_t frameCount);
        [[nodiscard]] bool BuildFrame(std::string& error);
        void RecordDraw(VkCommandBuffer commandBuffer);

    private:
        ImGuiContext* context_ = nullptr;
        VulkanDebugUiAccess::BuildFrame builder_ = nullptr;
        void* userdata_ = nullptr;
        VkDevice device_ = VK_NULL_HANDLE;
        VkDescriptorPool pool_ = VK_NULL_HANDLE;
        VkFormat format_ = VK_FORMAT_UNDEFINED;
        bool initialized_ = false;
    };
} // namespace owl::vulkan
