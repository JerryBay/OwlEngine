#include "VulkanAllocator.h"

#include "VulkanDevice.h"

#include <array>
#include <utility>

namespace owl::vulkan
{
    VulkanAllocator::~VulkanAllocator()
    {
        Reset();
    }

    VulkanAllocator::VulkanAllocator(VulkanAllocator&& other) noexcept
        : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)),
          device_(std::exchange(other.device_, VK_NULL_HANDLE)),
          memoryBudgetEnabled_(std::exchange(other.memoryBudgetEnabled_, false))
    {
    }

    VulkanAllocator& VulkanAllocator::operator=(VulkanAllocator&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            memoryBudgetEnabled_ = std::exchange(other.memoryBudgetEnabled_, false);
        }
        return *this;
    }

    std::optional<VulkanAllocator> VulkanAllocator::Create(
        const VkInstance instance, const VulkanDevice& device, std::string& error)
    {
        if (instance == VK_NULL_HANDLE || !device.IsValid())
        {
            error = "Cannot create an allocator without a valid instance and device";
            return std::nullopt;
        }
        const VmaVulkanFunctions functions{
            .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
            .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
        };
        const VmaAllocatorCreateInfo info{
            .flags = device.HasMemoryBudget()
                         ? VmaAllocatorCreateFlags{VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT}
                         : VmaAllocatorCreateFlags{0},
            .physicalDevice = device.PhysicalDevice(),
            .device = device.Get(),
            .pVulkanFunctions = &functions,
            .instance = instance,
            .vulkanApiVersion = VK_API_VERSION_1_3,
        };
        VulkanAllocator result;
        const VkResult created = vmaCreateAllocator(&info, &result.allocator_);
        if (created != VK_SUCCESS)
        {
            error = "vmaCreateAllocator failed with VkResult " +
                    std::to_string(static_cast<int>(created));
            return std::nullopt;
        }
        result.device_ = device.Get();
        result.memoryBudgetEnabled_ = device.HasMemoryBudget();
        error.clear();
        return result;
    }

    bool VulkanAllocator::IsValid() const noexcept { return allocator_ != VK_NULL_HANDLE; }
    VmaAllocator VulkanAllocator::Get() const noexcept { return allocator_; }
    VkDevice VulkanAllocator::Device() const noexcept { return device_; }

    std::optional<VulkanMemoryStats> VulkanAllocator::QueryMemoryStats() const noexcept
    {
        if (!IsValid())
            return std::nullopt;

        const VkPhysicalDeviceMemoryProperties* properties = nullptr;
        vmaGetMemoryProperties(allocator_, &properties);
        std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> heaps{};
        vmaGetHeapBudgets(allocator_, heaps.data());
        VulkanMemoryStats result;
        for (std::uint32_t index = 0; index < properties->memoryHeapCount; ++index)
        {
            // Only allocator-owned statistics; usage/budget describe a different scope.
            const auto& statistics = heaps[index].statistics;
            result.allocationCount += statistics.allocationCount;
            result.allocationBytes += statistics.allocationBytes;
            result.blockCount += statistics.blockCount;
            result.blockBytes += statistics.blockBytes;
        }
        return result;
    }

    std::optional<VulkanMemoryBudgetStats> VulkanAllocator::QueryMemoryBudgetStats() const noexcept
    {
        if (!IsValid() || !memoryBudgetEnabled_)
            return std::nullopt;

        const VkPhysicalDeviceMemoryProperties* properties = nullptr;
        vmaGetMemoryProperties(allocator_, &properties);
        std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> heaps{};
        vmaGetHeapBudgets(allocator_, heaps.data());

        VulkanMemoryBudgetStats result;
        static_assert(result.heaps.size() == VK_MAX_MEMORY_HEAPS);
        result.heapCount = properties->memoryHeapCount;
        for (std::uint32_t heapIndex = 0; heapIndex < result.heapCount; ++heapIndex)
        {
            auto& output = result.heaps[heapIndex];
            const auto& heap = properties->memoryHeaps[heapIndex];
            const auto& budget = heaps[heapIndex];
            output.heapIndex = heapIndex;
            output.heapFlags = heap.flags;
            output.deviceLocal = (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
            output.heapSize = heap.size;
            output.usage = budget.usage;
            output.budget = budget.budget;
            output.allocationCount = budget.statistics.allocationCount;
            output.allocationBytes = budget.statistics.allocationBytes;
            output.blockCount = budget.statistics.blockCount;
            output.blockBytes = budget.statistics.blockBytes;
        }
        return result;
    }

    void VulkanAllocator::SetCurrentFrameIndex(const std::uint32_t frameIndex) noexcept
    {
        if (IsValid() && memoryBudgetEnabled_)
            vmaSetCurrentFrameIndex(allocator_, frameIndex);
    }

    void VulkanAllocator::Reset() noexcept
    {
        if (allocator_ != VK_NULL_HANDLE)
            vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
        memoryBudgetEnabled_ = false;
    }
} // namespace owl::vulkan
