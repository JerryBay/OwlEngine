#include "VulkanBuffer.h"

#include "VulkanDevice.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>

namespace owl::vulkan::detail
{
    std::optional<std::uint32_t>
    SelectBufferMemoryType(const VkPhysicalDeviceMemoryProperties& properties,
                           const std::uint32_t memoryTypeBits,
                           const VkMemoryPropertyFlags required,
                           const VkMemoryPropertyFlags preferred) noexcept
    {
        std::optional<std::uint32_t> best;
        std::uint32_t bestScore = 0;
        for (std::uint32_t index = 0; index < properties.memoryTypeCount && index < 32; ++index)
        {
            if ((memoryTypeBits & (1U << index)) == 0)
                continue;
            const auto flags = properties.memoryTypes[index].propertyFlags;
            if ((flags & required) != required)
                continue;
            const auto score = static_cast<std::uint32_t>(std::popcount(flags & preferred));
            if (!best || score > bestScore)
            {
                best = index;
                bestScore = score;
            }
        }
        return best;
    }

    bool IsBufferRangeValid(const VkDeviceSize size, const VkDeviceSize offset,
                            const VkDeviceSize bytes) noexcept
    {
        return offset <= size && bytes <= size - offset;
    }
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    namespace
    {
        constexpr VkBufferUsageFlags AllowedUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        constexpr VkMemoryPropertyFlags AllowedMemory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

        bool Check(const VkResult result, const char* operation, std::string& error)
        {
            if (result == VK_SUCCESS)
                return true;
            error = std::string{operation} + " failed with VkResult " +
                    std::to_string(static_cast<int>(result));
            return false;
        }
    } // namespace

    VulkanBuffer::~VulkanBuffer()
    {
        Reset();
    }

    VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
        : device_(std::exchange(other.device_, nullptr)),
          buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
          memory_(std::exchange(other.memory_, VK_NULL_HANDLE)),
          size_(std::exchange(other.size_, 0)),
          allocationSize_(std::exchange(other.allocationSize_, 0)),
          memoryProperties_(std::exchange(other.memoryProperties_, 0))
    {
    }

    VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, nullptr);
            buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
            memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
            size_ = std::exchange(other.size_, 0);
            allocationSize_ = std::exchange(other.allocationSize_, 0);
            memoryProperties_ = std::exchange(other.memoryProperties_, 0);
        }
        return *this;
    }

    std::optional<VulkanBuffer> VulkanBuffer::Create(const VulkanDevice& device,
                                                     const VulkanBufferDesc& desc,
                                                     std::string& error)
    {
        if (!device.IsValid())
        {
            error = "Cannot create a buffer without a valid device";
            return std::nullopt;
        }
        if (desc.size == 0 || desc.usage == 0 || (desc.usage & ~AllowedUsage) != 0)
        {
            error = "Invalid Vulkan buffer size or usage";
            return std::nullopt;
        }
        if ((desc.requiredMemory & ~AllowedMemory) != 0 ||
            (desc.preferredMemory & ~AllowedMemory) != 0)
        {
            error = "Invalid Vulkan buffer memory preference flags";
            return std::nullopt;
        }

        VulkanBuffer result;
        result.device_ = &device;
        const VkBufferCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = desc.size,
            .usage = desc.usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        if (!Check(vkCreateBuffer(device.Get(), &info, nullptr, &result.buffer_),
                   "vkCreateBuffer", error))
            return std::nullopt;

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device.Get(), result.buffer_, &requirements);
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(device.PhysicalDevice(), &properties);
        const auto memoryType = detail::SelectBufferMemoryType(
            properties, requirements.memoryTypeBits, desc.requiredMemory, desc.preferredMemory);
        if (!memoryType)
        {
            error = "Buffer has no compatible memory type";
            return std::nullopt;
        }
        const VkMemoryAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = *memoryType,
        };
        if (!Check(vkAllocateMemory(device.Get(), &allocate, nullptr, &result.memory_),
                   "vkAllocateMemory", error))
            return std::nullopt;
        if (!Check(vkBindBufferMemory(device.Get(), result.buffer_, result.memory_, 0),
                   "vkBindBufferMemory", error))
            return std::nullopt;

        result.size_ = desc.size;
        result.allocationSize_ = requirements.size;
        result.memoryProperties_ = properties.memoryTypes[*memoryType].propertyFlags;
        error.clear();
        return result;
    }

    bool VulkanBuffer::IsValid() const noexcept { return buffer_ != VK_NULL_HANDLE; }
    VkBuffer VulkanBuffer::Get() const noexcept { return buffer_; }
    VkDeviceSize VulkanBuffer::Size() const noexcept { return size_; }
    VkDeviceSize VulkanBuffer::AllocationSize() const noexcept { return allocationSize_; }
    VkMemoryPropertyFlags VulkanBuffer::MemoryProperties() const noexcept
    {
        return memoryProperties_;
    }

    bool VulkanBuffer::Write(const VkDeviceSize offset, const std::span<const std::byte> data,
                             std::string& error)
    {
        if ((memoryProperties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0 ||
            !detail::IsBufferRangeValid(size_, offset, data.size()))
        {
            error = "Buffer write requires host-visible memory and a valid range";
            return false;
        }
        if (data.empty())
        {
            error.clear();
            return true;
        }
        void* mapped = nullptr;
        if (!Check(vkMapMemory(device_->Get(), memory_, 0, VK_WHOLE_SIZE, 0, &mapped),
                   "vkMapMemory", error))
            return false;
        std::memcpy(static_cast<std::byte*>(mapped) + offset, data.data(), data.size());
        bool success = true;
        if ((memoryProperties_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
        {
            const VkMappedMemoryRange range{
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = memory_,
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            };
            success = Check(vkFlushMappedMemoryRanges(device_->Get(), 1, &range),
                            "vkFlushMappedMemoryRanges", error);
        }
        vkUnmapMemory(device_->Get(), memory_);
        if (success)
            error.clear();
        return success;
    }

    bool VulkanBuffer::Read(const VkDeviceSize offset, const std::span<std::byte> data,
                            std::string& error)
    {
        if ((memoryProperties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0 ||
            !detail::IsBufferRangeValid(size_, offset, data.size()))
        {
            error = "Buffer read requires host-visible memory and a valid range";
            return false;
        }
        if (data.empty())
        {
            error.clear();
            return true;
        }
        void* mapped = nullptr;
        if (!Check(vkMapMemory(device_->Get(), memory_, 0, VK_WHOLE_SIZE, 0, &mapped),
                   "vkMapMemory", error))
            return false;
        bool success = true;
        if ((memoryProperties_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
        {
            const VkMappedMemoryRange range{
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = memory_,
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            };
            success = Check(vkInvalidateMappedMemoryRanges(device_->Get(), 1, &range),
                            "vkInvalidateMappedMemoryRanges", error);
        }
        if (success)
            std::memcpy(data.data(), static_cast<const std::byte*>(mapped) + offset, data.size());
        vkUnmapMemory(device_->Get(), memory_);
        if (success)
            error.clear();
        return success;
    }

    void VulkanBuffer::Reset() noexcept
    {
        if (device_ != nullptr && device_->IsValid())
        {
            vkDestroyBuffer(device_->Get(), buffer_, nullptr);
            vkFreeMemory(device_->Get(), memory_, nullptr);
        }
        device_ = nullptr;
        buffer_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        size_ = 0;
        allocationSize_ = 0;
        memoryProperties_ = 0;
    }
} // namespace owl::vulkan
