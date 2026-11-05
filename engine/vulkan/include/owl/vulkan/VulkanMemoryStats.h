#pragma once

#include <cstdint>

namespace owl::vulkan
{
    // Totals for one VMA allocator across its heaps, not process VRAM or logical payload.
    // Excludes allocations owned outside VMA (including swapchain images and descriptors).
    struct VulkanMemoryStats
    {
        std::uint64_t allocationCount = 0;
        std::uint64_t allocationBytes = 0;
        std::uint64_t blockCount = 0;
        // Reserved VkDeviceMemory storage, including cached free space.
        std::uint64_t blockBytes = 0;
    };
} // namespace owl::vulkan
