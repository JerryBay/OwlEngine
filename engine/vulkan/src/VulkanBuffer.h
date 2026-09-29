#pragma once

#include "VulkanMemory.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace owl::vulkan
{
    class VulkanAllocator;
    class VulkanDevice;

    enum class BufferHostAccess
    {
        None,
        SequentialWrite,
        Random,
    };

    struct VulkanBufferDesc
    {
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = 0;
        VkMemoryPropertyFlags requiredMemory = 0;
        VkMemoryPropertyFlags preferredMemory = 0;
        BufferHostAccess hostAccess = BufferHostAccess::None;
    };

    namespace detail
    {
        [[nodiscard]] bool IsBufferDescValid(const VulkanBufferDesc& desc) noexcept;

        [[nodiscard]] bool IsBufferRangeValid(VkDeviceSize size, VkDeviceSize offset,
                                              VkDeviceSize bytes) noexcept;
    } // namespace detail

    // Owns the buffer/allocation; borrows the native allocator, which must outlive it.
    // Finish GPU use before destruction/move replacement. CPU access requires synchronization.
    class VulkanBuffer
    {
    public:
        VulkanBuffer() noexcept = default;
        ~VulkanBuffer();
        VulkanBuffer(const VulkanBuffer&) = delete;
        VulkanBuffer& operator=(const VulkanBuffer&) = delete;
        VulkanBuffer(VulkanBuffer&& other) noexcept;
        VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

        [[nodiscard]] static std::optional<VulkanBuffer>
        Create(const VulkanAllocator& allocator, const VulkanBufferDesc& desc, std::string& error);
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VkBuffer Get() const noexcept;
        [[nodiscard]] VkDeviceSize Size() const noexcept;
        // The allocation slice size, not the containing VMA memory block size.
        [[nodiscard]] VkDeviceSize AllocationSize() const noexcept;
        [[nodiscard]] VkMemoryPropertyFlags MemoryProperties() const noexcept;
        [[nodiscard]] bool Write(VkDeviceSize offset, std::span<const std::byte> data,
                                  std::string& error);
        [[nodiscard]] bool Read(VkDeviceSize offset, std::span<std::byte> data,
                                 std::string& error);

    private:
        void Reset() noexcept;

        VmaAllocator allocator_ = VK_NULL_HANDLE;
        VkBuffer buffer_ = VK_NULL_HANDLE;
        VmaAllocation allocation_ = VK_NULL_HANDLE;
        VkDeviceSize size_ = 0;
        VkDeviceSize allocationSize_ = 0;
        VkMemoryPropertyFlags memoryProperties_ = 0;
        BufferHostAccess hostAccess_ = BufferHostAccess::None;
    };
} // namespace owl::vulkan
