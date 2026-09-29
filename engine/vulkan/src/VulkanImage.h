#pragma once

#include "VulkanMemory.h"

#include <cstdint>
#include <optional>
#include <string>

namespace owl::vulkan
{
    class VulkanAllocator;

    // Ordinary sampled 2D color images only: optimal tiling, one layer and one sample.
    struct VulkanImageDesc
    {
        VkExtent2D extent{};
        VkFormat format = VK_FORMAT_UNDEFINED;
        std::uint32_t mipLevels = 1;
        VkImageUsageFlags usage = 0;
    };

    namespace detail
    {
        [[nodiscard]] bool IsImageDescValid(const VulkanImageDesc& desc) noexcept;
        [[nodiscard]] bool IsImageSupported(const VulkanImageDesc& desc,
                                             const VkImageFormatProperties& limits) noexcept;
    } // namespace detail

    // Owns image/allocation; allocator and device must outlive it. Release all views and finish
    // GPU use before destruction or move replacement. No implicit wait or layout transition.
    // Moving this wrapper preserves native identity and does not invalidate existing views.
    class VulkanImage
    {
    public:
        VulkanImage() noexcept = default;
        ~VulkanImage();
        VulkanImage(const VulkanImage&) = delete;
        VulkanImage& operator=(const VulkanImage&) = delete;
        VulkanImage(VulkanImage&& other) noexcept;
        VulkanImage& operator=(VulkanImage&& other) noexcept;

        [[nodiscard]] static std::optional<VulkanImage>
        Create(const VulkanAllocator& allocator, const VulkanImageDesc& desc, std::string& error);
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VkImage Get() const noexcept;
        [[nodiscard]] VkDevice Device() const noexcept;
        [[nodiscard]] const VulkanImageDesc& Desc() const noexcept;

    private:
        void Reset() noexcept;

        VmaAllocator allocator_ = VK_NULL_HANDLE;
        VmaAllocation allocation_ = VK_NULL_HANDLE;
        VkImage image_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
        VulkanImageDesc desc_{};
    };
} // namespace owl::vulkan
