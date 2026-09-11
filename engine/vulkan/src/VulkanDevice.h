#pragma once

#include "VulkanDeviceSelection.h"
#include "VulkanPresentationSupport.h"

#include <optional>
#include <string>
#include <vector>

namespace owl::vulkan
{
    namespace detail
    {
        [[nodiscard]] std::optional<std::vector<std::uint32_t>>
        BuildDeviceQueueFamilyIndices(const QueueFamilySelection& selection, std::string& error);
    } // namespace detail

    // Owns VkDevice; physical device and queues are borrowed. VkInstance must outlive this owner.
    // Before destruction or move assignment, finish GPU work, destroy this device's children,
    // and ensure no concurrent host access to the device or its queues. No implicit wait is made.
    class VulkanDevice
    {
    public:
        VulkanDevice() noexcept = default;
        ~VulkanDevice();

        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        VulkanDevice(VulkanDevice&& other) noexcept;
        VulkanDevice& operator=(VulkanDevice&& other) noexcept;

        [[nodiscard]] static std::optional<VulkanDevice>
        Create(const VulkanDeviceSelection& selection, std::string& error,
               const detail::PresentationSupportCapabilities& presentationSupport = {});

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] VkDevice Get() const noexcept;
        [[nodiscard]] VkPhysicalDevice PhysicalDevice() const noexcept;
        [[nodiscard]] VkQueue GraphicsQueue() const noexcept;
        [[nodiscard]] VkQueue PresentQueue() const noexcept;
        [[nodiscard]] bool HasPresentFences() const noexcept;
        [[nodiscard]] const detail::QueueFamilySelection& QueueFamilies() const noexcept;

    private:
        void Reset() noexcept;

        VkDevice device_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkQueue graphicsQueue_ = VK_NULL_HANDLE;
        VkQueue presentQueue_ = VK_NULL_HANDLE;
        detail::QueueFamilySelection queueFamilies_{};
        detail::SwapchainMaintenance swapchainMaintenance_ = detail::SwapchainMaintenance::None;
    };
} // namespace owl::vulkan
