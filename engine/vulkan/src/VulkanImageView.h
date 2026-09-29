#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <string>

namespace owl::vulkan
{
    class VulkanImage;

    struct VulkanImageViewDesc
    {
        std::uint32_t baseMipLevel = 0;
        std::uint32_t levelCount = 1;
    };

    namespace detail
    {
        [[nodiscard]] bool IsImageViewRangeValid(std::uint32_t mipLevels,
                                                 const VulkanImageViewDesc& desc) noexcept;
    } // namespace detail

    // Owns a same-format 2D color view; borrows the native image/device, not wrapper addresses.
    // The image/device must outlive it; finish GPU use before destruction or move replacement.
    class VulkanImageView
    {
    public:
        VulkanImageView() noexcept = default;
        ~VulkanImageView();
        VulkanImageView(const VulkanImageView&) = delete;
        VulkanImageView& operator=(const VulkanImageView&) = delete;
        VulkanImageView(VulkanImageView&& other) noexcept;
        VulkanImageView& operator=(VulkanImageView&& other) noexcept;

        [[nodiscard]] static std::optional<VulkanImageView>
        Create(const VulkanImage& image, const VulkanImageViewDesc& desc, std::string& error);
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VkImageView Get() const noexcept;

    private:
        void Reset() noexcept;

        VkDevice device_ = VK_NULL_HANDLE;
        VkImageView view_ = VK_NULL_HANDLE;
    };
} // namespace owl::vulkan
