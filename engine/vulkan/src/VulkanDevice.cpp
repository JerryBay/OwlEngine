#include "VulkanDevice.h"

#include <owl/foundation/Log.h>

#include <array>
#include <utility>

namespace owl::vulkan
{
    namespace detail
    {
        std::optional<std::vector<std::uint32_t>>
        BuildDeviceQueueFamilyIndices(const QueueFamilySelection& selection, std::string& error)
        {
            if (!selection.graphicsFamily.has_value())
            {
                error = "Cannot create a Vulkan device without a graphics queue family";
                return std::nullopt;
            }
            if (!selection.presentFamily.has_value())
            {
                error = "Cannot create a Vulkan device without a present queue family";
                return std::nullopt;
            }

            std::vector<std::uint32_t> families{*selection.graphicsFamily};
            if (!selection.UsesUnifiedFamily())
            {
                families.push_back(*selection.presentFamily);
            }
            error.clear();
            return families;
        }
    } // namespace detail

    VulkanDevice::~VulkanDevice()
    {
        Reset();
    }

    VulkanDevice::VulkanDevice(VulkanDevice&& other) noexcept
        : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
          graphicsQueue_(std::exchange(other.graphicsQueue_, VK_NULL_HANDLE)),
          presentQueue_(std::exchange(other.presentQueue_, VK_NULL_HANDLE)),
          queueFamilies_(std::exchange(other.queueFamilies_, detail::QueueFamilySelection{}))
    {
    }

    VulkanDevice& VulkanDevice::operator=(VulkanDevice&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            graphicsQueue_ = std::exchange(other.graphicsQueue_, VK_NULL_HANDLE);
            presentQueue_ = std::exchange(other.presentQueue_, VK_NULL_HANDLE);
            queueFamilies_ = std::exchange(other.queueFamilies_, detail::QueueFamilySelection{});
        }
        return *this;
    }

    std::optional<VulkanDevice> VulkanDevice::Create(const VulkanDeviceSelection& selection,
                                                     std::string& error)
    {
        if (selection.Get() == VK_NULL_HANDLE)
        {
            error = "Cannot create a Vulkan device without a valid VkPhysicalDevice";
            return std::nullopt;
        }

        const auto families =
            detail::BuildDeviceQueueFamilyIndices(selection.QueueFamilies(), error);
        if (!families.has_value())
        {
            return std::nullopt;
        }

        const float queuePriority = 1.0F;
        std::array<VkDeviceQueueCreateInfo, 2> queueCreateInfos{};
        for (std::size_t index = 0; index < families->size(); ++index)
        {
            queueCreateInfos[index] = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = (*families)[index],
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            };
        }

        VkPhysicalDeviceVulkan13Features features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE,
        };
        constexpr std::array extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        const VkDeviceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &features,
            .queueCreateInfoCount = static_cast<std::uint32_t>(families->size()),
            .pQueueCreateInfos = queueCreateInfos.data(),
            .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
        };

        VkDevice handle = VK_NULL_HANDLE;
        const VkResult result = vkCreateDevice(selection.Get(), &createInfo, nullptr, &handle);
        if (result != VK_SUCCESS)
        {
            error =
                "vkCreateDevice failed with VkResult " + std::to_string(static_cast<int>(result));
            owl::foundation::LogMessage(owl::foundation::LogLevel::Error, "Vulkan", error);
            return std::nullopt;
        }

        VulkanDevice device;
        device.device_ = handle;
        device.queueFamilies_ = selection.QueueFamilies();
        vkGetDeviceQueue(device.device_, *device.queueFamilies_.graphicsFamily, 0,
                         &device.graphicsQueue_);
        vkGetDeviceQueue(device.device_, *device.queueFamilies_.presentFamily, 0,
                         &device.presentQueue_);

        owl::foundation::LogMessage(
            owl::foundation::LogLevel::Info, "Vulkan",
            "Created logical device for '" + std::string{selection.Properties().deviceName} +
                "' (queueFamilyCount=" + std::to_string(families->size()) +
                ", graphicsQueueFamily=" + std::to_string(*device.queueFamilies_.graphicsFamily) +
                ", presentQueueFamily=" + std::to_string(*device.queueFamilies_.presentFamily) +
                ", extension=VK_KHR_swapchain, dynamicRendering=true, synchronization2=true)");
        error.clear();
        return device;
    }

    bool VulkanDevice::IsValid() const noexcept
    {
        return device_ != VK_NULL_HANDLE;
    }

    VkDevice VulkanDevice::Get() const noexcept
    {
        return device_;
    }

    VkQueue VulkanDevice::GraphicsQueue() const noexcept
    {
        return graphicsQueue_;
    }

    VkQueue VulkanDevice::PresentQueue() const noexcept
    {
        return presentQueue_;
    }

    const detail::QueueFamilySelection& VulkanDevice::QueueFamilies() const noexcept
    {
        return queueFamilies_;
    }

    void VulkanDevice::Reset() noexcept
    {
        if (device_ != VK_NULL_HANDLE)
        {
            vkDestroyDevice(device_, nullptr);
        }
        device_ = VK_NULL_HANDLE;
        graphicsQueue_ = VK_NULL_HANDLE;
        presentQueue_ = VK_NULL_HANDLE;
        queueFamilies_ = {};
    }
} // namespace owl::vulkan
