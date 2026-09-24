#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace owl::vulkan
{
    class VulkanDevice;

    struct VulkanBufferDesc
    {
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = 0;
        VkMemoryPropertyFlags requiredMemory = 0;
        VkMemoryPropertyFlags preferredMemory = 0;
    };

    namespace detail
    {
        [[nodiscard]] std::optional<std::uint32_t>
        SelectBufferMemoryType(const VkPhysicalDeviceMemoryProperties& properties,
                               std::uint32_t memoryTypeBits, VkMemoryPropertyFlags required,
                               VkMemoryPropertyFlags preferred) noexcept;

        [[nodiscard]] bool IsBufferRangeValid(VkDeviceSize size, VkDeviceSize offset,
                                              VkDeviceSize bytes) noexcept;
    } // namespace detail

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
        Create(const VulkanDevice& device, const VulkanBufferDesc& desc, std::string& error);
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VkBuffer Get() const noexcept;
        [[nodiscard]] VkDeviceSize Size() const noexcept;
        [[nodiscard]] VkDeviceSize AllocationSize() const noexcept;
        [[nodiscard]] VkMemoryPropertyFlags MemoryProperties() const noexcept;
        [[nodiscard]] bool Write(VkDeviceSize offset, std::span<const std::byte> data,
                                  std::string& error);
        [[nodiscard]] bool Read(VkDeviceSize offset, std::span<std::byte> data,
                                 std::string& error);

    private:
        void Reset() noexcept;

        const VulkanDevice* device_ = nullptr;
        VkBuffer buffer_ = VK_NULL_HANDLE;
        VkDeviceMemory memory_ = VK_NULL_HANDLE;
        VkDeviceSize size_ = 0;
        VkDeviceSize allocationSize_ = 0;
        VkMemoryPropertyFlags memoryProperties_ = 0;
    };
} // namespace owl::vulkan
