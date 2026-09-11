#include "VulkanDevice.h"

#include <owl/foundation/Log.h>

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace owl::vulkan
{
    namespace
    {
        [[nodiscard]] bool
        HasExtension(const std::span<const VkExtensionProperties> availableExtensions,
                     const std::string_view requestedName)
        {
            return std::ranges::any_of(
                availableExtensions, [requestedName](const auto& extension)
                { return std::string_view{extension.extensionName} == requestedName; });
        }

        [[nodiscard]] bool EnumerateDeviceExtensions(const VkPhysicalDevice physicalDevice,
                                                     std::vector<VkExtensionProperties>& extensions,
                                                     std::string& error)
        {
            std::uint32_t count = 0;
            VkResult result =
                vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
            if (result != VK_SUCCESS)
            {
                error = "vkEnumerateDeviceExtensionProperties(count) failed with VkResult " +
                        std::to_string(static_cast<int>(result));
                return false;
            }

            do
            {
                extensions.resize(count);
                VkExtensionProperties* data = count > 0 ? extensions.data() : nullptr;
                result =
                    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, data);
            } while (result == VK_INCOMPLETE);

            if (result != VK_SUCCESS)
            {
                error = "vkEnumerateDeviceExtensionProperties(data) failed with VkResult " +
                        std::to_string(static_cast<int>(result));
                return false;
            }

            extensions.resize(count);
            return true;
        }
    } // namespace

    namespace detail
    {
        SwapchainMaintenance
        SelectSwapchainMaintenance(const PresentationSupportCapabilities& instanceSupport,
                                   const SwapchainMaintenanceAvailability& deviceSupport) noexcept
        {
            if (!deviceSupport.feature)
            {
                return SwapchainMaintenance::None;
            }
            if (instanceSupport.khrSurfaceMaintenance1 && deviceSupport.khrExtension)
            {
                return SwapchainMaintenance::Khr;
            }
            if (instanceSupport.extSurfaceMaintenance1 && deviceSupport.extExtension)
            {
                return SwapchainMaintenance::Ext;
            }
            return SwapchainMaintenance::None;
        }

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
          physicalDevice_(std::exchange(other.physicalDevice_, VK_NULL_HANDLE)),
          graphicsQueue_(std::exchange(other.graphicsQueue_, VK_NULL_HANDLE)),
          presentQueue_(std::exchange(other.presentQueue_, VK_NULL_HANDLE)),
          queueFamilies_(std::exchange(other.queueFamilies_, detail::QueueFamilySelection{})),
          swapchainMaintenance_(
              std::exchange(other.swapchainMaintenance_, detail::SwapchainMaintenance::None))
    {
    }

    VulkanDevice& VulkanDevice::operator=(VulkanDevice&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            physicalDevice_ = std::exchange(other.physicalDevice_, VK_NULL_HANDLE);
            graphicsQueue_ = std::exchange(other.graphicsQueue_, VK_NULL_HANDLE);
            presentQueue_ = std::exchange(other.presentQueue_, VK_NULL_HANDLE);
            queueFamilies_ = std::exchange(other.queueFamilies_, detail::QueueFamilySelection{});
            swapchainMaintenance_ =
                std::exchange(other.swapchainMaintenance_, detail::SwapchainMaintenance::None);
        }
        return *this;
    }

    std::optional<VulkanDevice>
    VulkanDevice::Create(const VulkanDeviceSelection& selection, std::string& error,
                         const detail::PresentationSupportCapabilities& presentationSupport)
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

        detail::SwapchainMaintenance maintenance = detail::SwapchainMaintenance::None;
        VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR maintenanceFeatures{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR,
        };
        if (presentationSupport.khrSurfaceMaintenance1 ||
            presentationSupport.extSurfaceMaintenance1)
        {
            std::vector<VkExtensionProperties> availableExtensions;
            if (!EnumerateDeviceExtensions(selection.Get(), availableExtensions, error))
            {
                return std::nullopt;
            }

            detail::SwapchainMaintenanceAvailability maintenanceAvailability{
                .khrExtension = HasExtension(availableExtensions,
                                             VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME),
                .extExtension = HasExtension(availableExtensions,
                                             VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME),
            };
            if ((presentationSupport.khrSurfaceMaintenance1 &&
                 maintenanceAvailability.khrExtension) ||
                (presentationSupport.extSurfaceMaintenance1 &&
                 maintenanceAvailability.extExtension))
            {
                VkPhysicalDeviceFeatures2 availableFeatures{
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                    .pNext = &maintenanceFeatures,
                };
                vkGetPhysicalDeviceFeatures2(selection.Get(), &availableFeatures);
                maintenanceAvailability.feature =
                    maintenanceFeatures.swapchainMaintenance1 == VK_TRUE;
            }
            maintenance =
                detail::SelectSwapchainMaintenance(presentationSupport, maintenanceAvailability);
        }

        maintenanceFeatures.swapchainMaintenance1 =
            maintenance != detail::SwapchainMaintenance::None ? VK_TRUE : VK_FALSE;
        VkPhysicalDeviceVulkan13Features features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext =
                maintenance != detail::SwapchainMaintenance::None ? &maintenanceFeatures : nullptr,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE,
        };
        std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (maintenance == detail::SwapchainMaintenance::Khr)
        {
            extensions.push_back(VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        }
        else if (maintenance == detail::SwapchainMaintenance::Ext)
        {
            extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        }
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
        device.physicalDevice_ = selection.Get();
        device.swapchainMaintenance_ = maintenance;
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
                ", extension=VK_KHR_swapchain, presentFences=" +
                (device.HasPresentFences() ? "true" : "false") +
                ", dynamicRendering=true, synchronization2=true)");
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

    VkPhysicalDevice VulkanDevice::PhysicalDevice() const noexcept
    {
        return physicalDevice_;
    }

    VkQueue VulkanDevice::PresentQueue() const noexcept
    {
        return presentQueue_;
    }

    bool VulkanDevice::HasPresentFences() const noexcept
    {
        return swapchainMaintenance_ != detail::SwapchainMaintenance::None;
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
        physicalDevice_ = VK_NULL_HANDLE;
        graphicsQueue_ = VK_NULL_HANDLE;
        presentQueue_ = VK_NULL_HANDLE;
        queueFamilies_ = {};
        swapchainMaintenance_ = detail::SwapchainMaintenance::None;
    }
} // namespace owl::vulkan
