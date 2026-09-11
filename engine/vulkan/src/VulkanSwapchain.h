#pragma once

#include "VulkanDeviceSelection.h"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace owl::vulkan
{
    class VulkanDevice;
    class VulkanSurface;

    enum class SwapchainUpdateResult
    {
        Ready,
        Deferred,
        Failed,
    };

    namespace detail
    {
        struct SwapchainSupport
        {
            VkSurfaceCapabilitiesKHR capabilities{};
            std::span<const VkSurfaceFormatKHR> formats;
            std::span<const VkPresentModeKHR> presentModes;
        };

        struct SwapchainConfiguration
        {
            VkSurfaceFormatKHR surfaceFormat{};
            VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
            VkExtent2D extent{};
            std::uint32_t minImageCount = 0;
            VkSurfaceTransformFlagBitsKHR preTransform{};
            VkCompositeAlphaFlagBitsKHR compositeAlpha{};
            VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            std::array<std::uint32_t, 2> queueFamilyIndices{};
            std::uint32_t queueFamilyIndexCount = 0;
        };

        [[nodiscard]] SwapchainUpdateResult SelectSwapchainConfiguration(
            const SwapchainSupport& support, const QueueFamilySelection& queueFamilies,
            VkExtent2D requestedExtent, SwapchainConfiguration& configuration, std::string& error);
    } // namespace detail

    // Owns the swapchain and its image views; images, Device and Surface are borrowed.
    // Device and Surface must share an Instance and outlive this object.
    // Before recreation, replacement or destruction,
    // finish all uses of its images/views, release external dependents, and exclude concurrent
    // access. No implicit GPU wait is performed. Spans and handles are invalidated by recreation or
    // moves.
    class VulkanSwapchain
    {
    public:
        VulkanSwapchain() noexcept = default;
        ~VulkanSwapchain();

        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
        VulkanSwapchain(VulkanSwapchain&& other) noexcept;
        VulkanSwapchain& operator=(VulkanSwapchain&& other) noexcept;

        // Also creates an empty owner's first swapchain. A live owner cannot change Device/Surface.
        // Deferred preserves existing resources; callers must suspend image acquisition until
        // Ready. Failure before vkCreateSwapchainKHR preserves resources. Once called, the old
        // swapchain is retired even on failure, and is destroyed. Failure after that leaves an
        // empty owner.
        [[nodiscard]] SwapchainUpdateResult CreateOrRecreate(const VulkanDevice& device,
                                                             const VulkanSurface& surface,
                                                             VkExtent2D requestedExtent,
                                                             std::string& error);

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VkSwapchainKHR Get() const noexcept;
        [[nodiscard]] VkExtent2D Extent() const noexcept;
        [[nodiscard]] VkSurfaceFormatKHR SurfaceFormat() const noexcept;
        [[nodiscard]] VkPresentModeKHR PresentMode() const noexcept;
        [[nodiscard]] std::span<const VkImage> Images() const noexcept;
        [[nodiscard]] std::span<const VkImageView> ImageViews() const noexcept;

    private:
        void Reset() noexcept;

        VkDevice device_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
        detail::SwapchainConfiguration configuration_{};
        std::vector<VkImage> images_;
        std::vector<VkImageView> imageViews_;
    };
} // namespace owl::vulkan
