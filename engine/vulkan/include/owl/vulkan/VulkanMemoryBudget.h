#pragma once

#include <array>
#include <cstdint>

namespace owl::vulkan
{
    // Estimates from VMA with VK_EXT_memory_budget enabled; not raw driver measurements.
    // Usage can include process allocations outside this VMA allocator. Budget is a guideline.
    struct VulkanMemoryHeapBudget
    {
        std::uint32_t heapIndex = 0;
        // VkMemoryHeapFlags bits, not VkMemoryPropertyFlags of individual memory types.
        std::uint32_t heapFlags = 0;
        bool deviceLocal = false;
        std::uint64_t heapSize = 0;
        std::uint64_t usage = 0;
        std::uint64_t budget = 0;
        std::uint64_t allocationCount = 0;
        std::uint64_t allocationBytes = 0;
        std::uint64_t blockCount = 0;
        std::uint64_t blockBytes = 0;
    };

    // Per-heap device memory budget. The optional query is unavailable when the device did not
    // enable VK_EXT_memory_budget. VMA may still sanitize invalid driver values internally.
    struct VulkanMemoryBudgetStats
    {
        std::uint32_t heapCount = 0;
        // Vulkan's heap limit, checked against the pinned headers in the implementation.
        std::array<VulkanMemoryHeapBudget, 16> heaps{};
    };
} // namespace owl::vulkan
