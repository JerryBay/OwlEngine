#pragma once

#include <cstdint>

namespace owl::vulkan
{
    struct VulkanResourceUsage
    {
        std::uint64_t count = 0;
        // Buffer size or tightly packed RGBA8 pixels across all image mips.
        std::uint64_t requestedBytes = 0;
        // Live VMA allocation slices, excluding free space in reserved blocks.
        std::uint64_t allocationBytes = 0;

        VulkanResourceUsage& operator+=(const VulkanResourceUsage& other) noexcept
        {
            count += other.count;
            requestedBytes += other.requestedBytes;
            allocationBytes += other.allocationBytes;
            return *this;
        }
    };

    // Only resources owned through this renderer's VMA allocator. Includes upload destinations,
    // staging and retired images; excludes swapchain images, views, samplers and descriptors.
    struct VulkanResourceStats
    {
        VulkanResourceUsage buffers;
        VulkanResourceUsage images;
        // Subset of images.allocationBytes, not additional storage to add to the total.
        std::uint64_t retiredImageAllocationBytes = 0;

        VulkanResourceStats& operator+=(const VulkanResourceStats& other) noexcept
        {
            buffers += other.buffers;
            images += other.images;
            retiredImageAllocationBytes += other.retiredImageAllocationBytes;
            return *this;
        }
    };
} // namespace owl::vulkan
