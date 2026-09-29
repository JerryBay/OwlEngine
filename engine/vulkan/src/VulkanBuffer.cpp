#include "VulkanBuffer.h"

#include "VulkanAllocator.h"

#include <cstring>
#include <utility>

namespace owl::vulkan::detail
{
    bool IsBufferDescValid(const VulkanBufferDesc& desc) noexcept
    {
        constexpr VkBufferUsageFlags allowedUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        constexpr VkMemoryPropertyFlags allowedMemory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        if (desc.size == 0 || desc.usage == 0 || (desc.usage & ~allowedUsage) != 0 ||
            (desc.requiredMemory & ~allowedMemory) != 0 ||
            (desc.preferredMemory & ~allowedMemory) != 0)
            return false;

        const bool hostVisible = (desc.requiredMemory & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        switch (desc.hostAccess)
        {
        case BufferHostAccess::None:
            return !hostVisible;
        case BufferHostAccess::SequentialWrite:
        case BufferHostAccess::Random:
            return hostVisible;
        }
        return false;
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
        bool Check(const VkResult result, const char* operation, std::string& error)
        {
            if (result == VK_SUCCESS)
                return true;
            error = std::string{operation} + " failed with VkResult " +
                    std::to_string(static_cast<int>(result));
            return false;
        }

        // Also unmap if allocating an error string throws after a successful map.
        struct MappedAllocation
        {
            VmaAllocator allocator;
            VmaAllocation allocation;
            ~MappedAllocation() { vmaUnmapMemory(allocator, allocation); }
        };
    } // namespace

    VulkanBuffer::~VulkanBuffer()
    {
        Reset();
    }

    VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
        : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)),
          buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
          allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
          size_(std::exchange(other.size_, 0)),
          allocationSize_(std::exchange(other.allocationSize_, 0)),
          memoryProperties_(std::exchange(other.memoryProperties_, 0)),
          hostAccess_(std::exchange(other.hostAccess_, BufferHostAccess::None))
    {
    }

    VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
            buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
            allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
            size_ = std::exchange(other.size_, 0);
            allocationSize_ = std::exchange(other.allocationSize_, 0);
            memoryProperties_ = std::exchange(other.memoryProperties_, 0);
            hostAccess_ = std::exchange(other.hostAccess_, BufferHostAccess::None);
        }
        return *this;
    }

    std::optional<VulkanBuffer> VulkanBuffer::Create(const VulkanAllocator& allocator,
                                                     const VulkanBufferDesc& desc,
                                                     std::string& error)
    {
        if (!allocator.IsValid())
        {
            error = "Cannot create a buffer without a valid allocator";
            return std::nullopt;
        }
        if (!detail::IsBufferDescValid(desc))
        {
            error = "Invalid Vulkan buffer size, usage, memory flags, or host-access intent";
            return std::nullopt;
        }

        VulkanBuffer result;
        result.allocator_ = allocator.Get();
        const VkBufferCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = desc.size,
            .usage = desc.usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VmaAllocationCreateInfo allocate{
            .usage = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = desc.requiredMemory,
            .preferredFlags = desc.preferredMemory,
        };
        if (desc.hostAccess == BufferHostAccess::SequentialWrite)
            allocate.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        else if (desc.hostAccess == BufferHostAccess::Random)
            allocate.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

        VmaAllocationInfo allocationInfo{};
        if (!Check(vmaCreateBuffer(allocator.Get(), &info, &allocate, &result.buffer_,
                                   &result.allocation_, &allocationInfo),
                   "vmaCreateBuffer", error))
            return std::nullopt;

        result.size_ = desc.size;
        result.allocationSize_ = allocationInfo.size;
        result.hostAccess_ = desc.hostAccess;
        vmaGetAllocationMemoryProperties(allocator.Get(), result.allocation_,
                                         &result.memoryProperties_);
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
        if (!IsValid() || hostAccess_ == BufferHostAccess::None ||
            (memoryProperties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0 ||
            !detail::IsBufferRangeValid(size_, offset, data.size()))
        {
            error = "Buffer write requires writable host access and a valid range";
            return false;
        }
        if (data.empty())
        {
            error.clear();
            return true;
        }
        void* mapped = nullptr;
        if (!Check(vmaMapMemory(allocator_, allocation_, &mapped), "vmaMapMemory", error))
            return false;
        const MappedAllocation mapping{allocator_, allocation_};
        // VMA already offsets this pointer to our allocation, not the underlying memory block.
        std::memcpy(static_cast<std::byte*>(mapped) + offset, data.data(), data.size());
        if (!Check(vmaFlushAllocation(allocator_, allocation_, offset, data.size()),
                   "vmaFlushAllocation", error))
            return false;
        error.clear();
        return true;
    }

    bool VulkanBuffer::Read(const VkDeviceSize offset, const std::span<std::byte> data,
                            std::string& error)
    {
        if (!IsValid() || hostAccess_ != BufferHostAccess::Random ||
            (memoryProperties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0 ||
            !detail::IsBufferRangeValid(size_, offset, data.size()))
        {
            error = "Buffer read requires random host access and a valid range";
            return false;
        }
        if (data.empty())
        {
            error.clear();
            return true;
        }
        void* mapped = nullptr;
        if (!Check(vmaMapMemory(allocator_, allocation_, &mapped), "vmaMapMemory", error))
            return false;
        const MappedAllocation mapping{allocator_, allocation_};
        if (!Check(vmaInvalidateAllocation(allocator_, allocation_, offset, data.size()),
                   "vmaInvalidateAllocation", error))
            return false;
        std::memcpy(data.data(), static_cast<const std::byte*>(mapped) + offset, data.size());
        error.clear();
        return true;
    }

    void VulkanBuffer::Reset() noexcept
    {
        if (allocator_ != VK_NULL_HANDLE)
            vmaDestroyBuffer(allocator_, buffer_, allocation_);
        allocator_ = VK_NULL_HANDLE;
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        size_ = 0;
        allocationSize_ = 0;
        memoryProperties_ = 0;
        hostAccess_ = BufferHostAccess::None;
    }
} // namespace owl::vulkan
