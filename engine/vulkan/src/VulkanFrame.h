#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace owl::vulkan
{
    namespace detail
    {
        enum class AcquireAction
        {
            Render,
            Recreate,
            Defer,
            Fail
        };
        struct PresentDecision
        {
            bool enqueued = false;
            bool recreate = false;
            bool failed = false;
        };
        [[nodiscard]] AcquireAction ClassifyAcquireResult(VkResult result) noexcept;
        [[nodiscard]] PresentDecision ClassifyPresentResult(VkResult result) noexcept;
    } // namespace detail

    struct VulkanFrameSlot
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence complete = VK_NULL_HANDLE;
        // Also proves acquire has stopped using imageAvailable on pre-submit failure paths.
        VkFence acquired = VK_NULL_HANDLE;
        bool submissionPending = false;
        bool acquisitionPending = false;
    };

    struct VulkanPresentImage
    {
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence released = VK_NULL_HANDLE;
        bool presentPending = false;
    };

    // Borrows Device. The submitting owner must finish acquire/submit/present uses before
    // destruction or replacing presentation resources. Does not wait implicitly.
    class VulkanFrameResources
    {
    public:
        VulkanFrameResources() = default;
        ~VulkanFrameResources();
        VulkanFrameResources(const VulkanFrameResources&) = delete;
        VulkanFrameResources& operator=(const VulkanFrameResources&) = delete;

        [[nodiscard]] bool Initialize(VkDevice device, std::uint32_t graphicsFamily,
                                      std::string& error);
        [[nodiscard]] bool CreatePresentResources(std::size_t imageCount, bool presentFences,
                                                  std::string& error);
        void ResetPresentResources() noexcept;
        std::array<VulkanFrameSlot, 2> slots{};
        std::vector<VulkanPresentImage> images;

    private:
        VkDevice device_ = VK_NULL_HANDLE;
    };
} // namespace owl::vulkan
