#include "VulkanImage.h"

#include "VulkanAllocator.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace owl::vulkan::detail
{
    bool IsImageDescValid(const VulkanImageDesc& desc) noexcept
    {
        constexpr VkImageUsageFlags allowedUsage = VK_IMAGE_USAGE_SAMPLED_BIT |
                                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        return desc.extent.width > 0 && desc.extent.height > 0 &&
               (desc.format == VK_FORMAT_R8G8B8A8_UNORM || desc.format == VK_FORMAT_R8G8B8A8_SRGB) &&
               (desc.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 &&
               (desc.usage & ~allowedUsage) == 0 && desc.mipLevels > 0 &&
               desc.mipLevels <= static_cast<std::uint32_t>(
                   std::bit_width(std::max(desc.extent.width, desc.extent.height)));
    }

    bool IsImageSupported(const VulkanImageDesc& desc, const VkImageFormatProperties& limits) noexcept
    {
        return IsImageDescValid(desc) && desc.extent.width <= limits.maxExtent.width &&
               desc.extent.height <= limits.maxExtent.height && limits.maxExtent.depth >= 1 &&
               desc.mipLevels <= limits.maxMipLevels && limits.maxArrayLayers >= 1 &&
               (limits.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0;
    }
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    VulkanImage::~VulkanImage()
    {
        Reset();
    }

    VulkanImage::VulkanImage(VulkanImage&& other) noexcept
        : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)),
          allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
          image_(std::exchange(other.image_, VK_NULL_HANDLE)),
          device_(std::exchange(other.device_, VK_NULL_HANDLE)),
          desc_(std::exchange(other.desc_, VulkanImageDesc{}))
    {
    }

    VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
            allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
            image_ = std::exchange(other.image_, VK_NULL_HANDLE);
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            desc_ = std::exchange(other.desc_, VulkanImageDesc{});
        }
        return *this;
    }

    std::optional<VulkanImage> VulkanImage::Create(const VulkanAllocator& allocator,
                                                  const VulkanImageDesc& desc,
                                                  std::string& error)
    {
        if (!allocator.IsValid() || !detail::IsImageDescValid(desc))
        {
            error = "Image creation requires a valid allocator and supported 2D color description";
            return std::nullopt;
        }
        VmaAllocatorInfo parents{};
        vmaGetAllocatorInfo(allocator.Get(), &parents);
        VkImageFormatProperties limits{};
        const VkResult supported = vkGetPhysicalDeviceImageFormatProperties(
            parents.physicalDevice, desc.format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            desc.usage, 0, &limits);
        if (supported != VK_SUCCESS)
        {
            error = "vkGetPhysicalDeviceImageFormatProperties failed with VkResult " +
                    std::to_string(static_cast<int>(supported));
            return std::nullopt;
        }
        if (!detail::IsImageSupported(desc, limits))
        {
            error = "Image description exceeds the physical device's format limits";
            return std::nullopt;
        }

        const VkImageCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = desc.format,
            .extent = {desc.extent.width, desc.extent.height, 1},
            .mipLevels = desc.mipLevels,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = desc.usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        const VmaAllocationCreateInfo allocate{
            .usage = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        };
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        const VkResult created = vmaCreateImage(allocator.Get(), &info, &allocate,
                                               &image, &allocation, nullptr);
        if (created != VK_SUCCESS)
        {
            error = "vmaCreateImage failed with VkResult " +
                    std::to_string(static_cast<int>(created));
            return std::nullopt;
        }
        // Only successful outputs become owned. VMA unwinds its own partial creation failures.
        VulkanImage result;
        result.allocator_ = allocator.Get();
        result.device_ = parents.device;
        result.image_ = image;
        result.allocation_ = allocation;
        result.desc_ = desc;
        error.clear();
        return result;
    }

    bool VulkanImage::IsValid() const noexcept { return image_ != VK_NULL_HANDLE; }
    VkImage VulkanImage::Get() const noexcept { return image_; }
    VkDevice VulkanImage::Device() const noexcept { return device_; }
    const VulkanImageDesc& VulkanImage::Desc() const noexcept { return desc_; }

    void VulkanImage::Reset() noexcept
    {
        if (allocator_ != VK_NULL_HANDLE)
            vmaDestroyImage(allocator_, image_, allocation_);
        allocator_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        image_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
        desc_ = {};
    }
} // namespace owl::vulkan
