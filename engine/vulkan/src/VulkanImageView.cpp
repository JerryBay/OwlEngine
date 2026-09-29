#include "VulkanImageView.h"

#include "VulkanImage.h"

#include <utility>

namespace owl::vulkan::detail
{
    bool IsImageViewRangeValid(const std::uint32_t mipLevels,
                               const VulkanImageViewDesc& desc) noexcept
    {
        return desc.levelCount > 0 && desc.levelCount != VK_REMAINING_MIP_LEVELS &&
               desc.baseMipLevel < mipLevels && desc.levelCount <= mipLevels - desc.baseMipLevel;
    }
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    VulkanImageView::~VulkanImageView()
    {
        Reset();
    }

    VulkanImageView::VulkanImageView(VulkanImageView&& other) noexcept
        : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
          view_(std::exchange(other.view_, VK_NULL_HANDLE))
    {
    }

    VulkanImageView& VulkanImageView::operator=(VulkanImageView&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            view_ = std::exchange(other.view_, VK_NULL_HANDLE);
        }
        return *this;
    }

    std::optional<VulkanImageView> VulkanImageView::Create(const VulkanImage& image,
                                                          const VulkanImageViewDesc& desc,
                                                          std::string& error)
    {
        if (!image.IsValid() || !detail::IsImageViewRangeValid(image.Desc().mipLevels, desc))
        {
            error = "Image view requires a valid image and a nonempty mip range within it";
            return std::nullopt;
        }
        const VkImageViewCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image.Get(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image.Desc().format,
            .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = desc.baseMipLevel,
                .levelCount = desc.levelCount,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        VkImageView view = VK_NULL_HANDLE;
        const VkResult created = vkCreateImageView(image.Device(), &info, nullptr, &view);
        if (created != VK_SUCCESS)
        {
            error = "vkCreateImageView(texture) failed with VkResult " +
                    std::to_string(static_cast<int>(created));
            return std::nullopt;
        }
        VulkanImageView result;
        result.device_ = image.Device();
        result.view_ = view;
        error.clear();
        return result;
    }

    bool VulkanImageView::IsValid() const noexcept { return view_ != VK_NULL_HANDLE; }
    VkImageView VulkanImageView::Get() const noexcept { return view_; }

    void VulkanImageView::Reset() noexcept
    {
        if (view_ != VK_NULL_HANDLE)
            vkDestroyImageView(device_, view_, nullptr);
        view_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
    }
} // namespace owl::vulkan
