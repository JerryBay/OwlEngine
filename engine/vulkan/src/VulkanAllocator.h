#pragma once

#include "VulkanMemory.h"

#include <optional>
#include <string>

namespace owl::vulkan
{
    class VulkanDevice;

    // Owns the allocator, borrows the instance and device. All allocations and their GPU use
    // must end before destruction or move replacement. Moving preserves the native handle.
    class VulkanAllocator
    {
    public:
        VulkanAllocator() noexcept = default;
        ~VulkanAllocator();
        VulkanAllocator(const VulkanAllocator&) = delete;
        VulkanAllocator& operator=(const VulkanAllocator&) = delete;
        VulkanAllocator(VulkanAllocator&& other) noexcept;
        VulkanAllocator& operator=(VulkanAllocator&& other) noexcept;

        [[nodiscard]] static std::optional<VulkanAllocator>
        Create(VkInstance instance, const VulkanDevice& device, std::string& error);
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VmaAllocator Get() const noexcept;
        [[nodiscard]] VkDevice Device() const noexcept;

    private:
        void Reset() noexcept;

        VmaAllocator allocator_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
    };
} // namespace owl::vulkan
