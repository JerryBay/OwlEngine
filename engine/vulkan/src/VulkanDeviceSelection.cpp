#include "VulkanDeviceSelection.h"

#include <owl/foundation/Log.h>

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace owl::vulkan
{
namespace
{
constexpr std::uint32_t RequiredApiVersion = VK_API_VERSION_1_3;
constexpr std::array RequiredDeviceExtensions{std::string_view{VK_KHR_SWAPCHAIN_EXTENSION_NAME}};
constexpr std::uint32_t MaxEnumerationAttempts = 8;

std::string VulkanError(const std::string_view operation, const VkResult result)
{
    return std::string{operation} + " failed with VkResult " +
           std::to_string(static_cast<int>(result));
}

std::string ApiVersionString(const std::uint32_t version)
{
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
           std::to_string(VK_API_VERSION_MINOR(version)) + "." +
           std::to_string(VK_API_VERSION_PATCH(version));
}

template <typename Value, typename Enumerate>
bool EnumerateVulkanValues(const std::string_view operation, Enumerate&& enumerate,
                           std::vector<Value>& values, std::string& error)
{
    for (std::uint32_t attempt = 0; attempt < MaxEnumerationAttempts; ++attempt)
    {
        std::uint32_t count = 0;
        const VkResult countResult = enumerate(&count, nullptr);
        if (countResult != VK_SUCCESS)
        {
            error = VulkanError(operation, countResult);
            return false;
        }

        values.resize(count);
        if (count == 0)
        {
            error.clear();
            return true;
        }

        std::uint32_t writtenCount = count;
        const VkResult valuesResult = enumerate(&writtenCount, values.data());
        if (valuesResult == VK_SUCCESS)
        {
            values.resize(writtenCount);
            error.clear();
            return true;
        }
        if (valuesResult != VK_INCOMPLETE)
        {
            error = VulkanError(operation, valuesResult);
            return false;
        }
    }

    error = VulkanError(operation, VK_INCOMPLETE) + " after repeated enumeration attempts";
    return false;
}

bool IsSrgbFormat(const VkFormat format) noexcept
{
    switch (format)
    {
    case VK_FORMAT_R8_SRGB:
    case VK_FORMAT_R8G8_SRGB:
    case VK_FORMAT_R8G8B8_SRGB:
    case VK_FORMAT_B8G8R8_SRGB:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return true;
    default:
        return false;
    }
}

struct EnumeratedCandidate
{
    VkPhysicalDeviceProperties properties{};
    std::vector<VkExtensionProperties> extensions;
    std::vector<detail::QueueFamilyInfo> queueFamilies;
    std::vector<VkSurfaceFormatKHR> surfaceFormats;
    std::vector<VkPresentModeKHR> presentModes;
    bool dynamicRendering = false;
    bool synchronization2 = false;
};

bool QueryCandidate(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface,
                    EnumeratedCandidate& candidate, std::string& error)
{
    vkGetPhysicalDeviceProperties(physicalDevice, &candidate.properties);

    const auto enumerateExtensions =
        [physicalDevice](std::uint32_t* count, VkExtensionProperties* extensions)
    { return vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, count, extensions); };
    if (!EnumerateVulkanValues("vkEnumerateDeviceExtensionProperties", enumerateExtensions,
                               candidate.extensions, error))
    {
        return false;
    }

    std::uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueProperties(queueFamilyCount);
    if (queueFamilyCount > 0)
    {
        std::uint32_t writtenQueueFamilyCount = queueFamilyCount;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &writtenQueueFamilyCount,
                                                 queueProperties.data());
        queueProperties.resize(writtenQueueFamilyCount);
    }

    candidate.queueFamilies.clear();
    candidate.queueFamilies.reserve(queueProperties.size());
    for (std::size_t index = 0; index < queueProperties.size(); ++index)
    {
        VkBool32 presentSupported = VK_FALSE;
        const VkResult presentResult = vkGetPhysicalDeviceSurfaceSupportKHR(
            physicalDevice, static_cast<std::uint32_t>(index), surface, &presentSupported);
        if (presentResult != VK_SUCCESS)
        {
            error = VulkanError(
                "vkGetPhysicalDeviceSurfaceSupportKHR(queueFamily=" + std::to_string(index) + ")",
                presentResult);
            return false;
        }

        candidate.queueFamilies.push_back({
            .queueCount = queueProperties[index].queueCount,
            .flags = queueProperties[index].queueFlags,
            .presentSupported = presentSupported == VK_TRUE,
        });
    }

    candidate.surfaceFormats.clear();
    candidate.presentModes.clear();
    // VUID-vkGetPhysicalDeviceSurfaceFormatsKHR-surface-06525 requires reported surface support
    // before querying formats; present-mode enumeration has the analogous requirement.
    if (detail::HasReportedSurfaceSupport(candidate.queueFamilies))
    {
        const auto enumerateSurfaceFormats =
            [physicalDevice, surface](std::uint32_t* count, VkSurfaceFormatKHR* formats)
        { return vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, count, formats); };
        if (!EnumerateVulkanValues("vkGetPhysicalDeviceSurfaceFormatsKHR", enumerateSurfaceFormats,
                                   candidate.surfaceFormats, error))
        {
            return false;
        }

        const auto enumeratePresentModes =
            [physicalDevice, surface](std::uint32_t* count, VkPresentModeKHR* presentModes)
        {
            return vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, count,
                                                             presentModes);
        };
        if (!EnumerateVulkanValues("vkGetPhysicalDeviceSurfacePresentModesKHR",
                                   enumeratePresentModes, candidate.presentModes, error))
        {
            return false;
        }
    }

    VkPhysicalDeviceVulkan13Features vulkan13Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    };
    VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan13Features,
    };
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
    candidate.dynamicRendering = vulkan13Features.dynamicRendering == VK_TRUE;
    candidate.synchronization2 = vulkan13Features.synchronization2 == VK_TRUE;

    error.clear();
    return true;
}

std::string DeviceName(const VkPhysicalDeviceProperties& properties)
{
    if (properties.deviceName[0] == '\0')
    {
        return "<unnamed physical device>";
    }
    return properties.deviceName;
}

std::string SelectedDeviceMessage(const VkPhysicalDeviceProperties& properties,
                                  const detail::DeviceEvaluation& evaluation)
{
    std::ostringstream message;
    message << "Selected physical device '" << DeviceName(properties) << "'"
            << " (api=" << ApiVersionString(properties.apiVersion)
            << ", graphicsQueueFamily=" << *evaluation.queueFamilies.graphicsFamily
            << ", presentQueueFamily=" << *evaluation.queueFamilies.presentFamily << ", queueMode="
            << (evaluation.queueFamilies.UsesUnifiedFamily() ? "unified" : "separate")
            << ", surfaceFormat=" << static_cast<int>(evaluation.surfaceFormat.format)
            << ", colorSpace=" << static_cast<int>(evaluation.surfaceFormat.colorSpace)
            << ", presentMode=" << static_cast<int>(evaluation.presentMode)
            << ", dynamicRendering=true, synchronization2=true)";
    return message.str();
}
} // namespace

namespace detail
{
bool QueueFamilySelection::IsComplete() const noexcept
{
    return graphicsFamily.has_value() && presentFamily.has_value();
}

bool QueueFamilySelection::UsesUnifiedFamily() const noexcept
{
    return IsComplete() && graphicsFamily == presentFamily;
}

QueueFamilySelection
SelectQueueFamilies(const std::span<const QueueFamilyInfo> queueFamilies) noexcept
{
    for (std::size_t index = 0; index < queueFamilies.size(); ++index)
    {
        const QueueFamilyInfo& family = queueFamilies[index];
        if (family.queueCount > 0 && (family.flags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
            family.presentSupported)
        {
            const auto familyIndex = static_cast<std::uint32_t>(index);
            return {.graphicsFamily = familyIndex, .presentFamily = familyIndex};
        }
    }

    QueueFamilySelection selection;
    for (std::size_t index = 0; index < queueFamilies.size(); ++index)
    {
        const QueueFamilyInfo& family = queueFamilies[index];
        if (family.queueCount == 0)
        {
            continue;
        }

        const auto familyIndex = static_cast<std::uint32_t>(index);
        if (!selection.graphicsFamily.has_value() && (family.flags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            selection.graphicsFamily = familyIndex;
        }
        if (!selection.presentFamily.has_value() && family.presentSupported)
        {
            selection.presentFamily = familyIndex;
        }
    }
    return selection;
}

bool HasReportedSurfaceSupport(const std::span<const QueueFamilyInfo> queueFamilies) noexcept
{
    return std::ranges::any_of(queueFamilies, [](const QueueFamilyInfo& family)
                               { return family.presentSupported; });
}

std::optional<std::string_view>
FindMissingRequiredDeviceExtension(const std::span<const VkExtensionProperties> extensions) noexcept
{
    for (const std::string_view requiredExtension : RequiredDeviceExtensions)
    {
        const bool found = std::ranges::any_of(
            extensions, [requiredExtension](const auto& extension)
            { return std::string_view{extension.extensionName} == requiredExtension; });
        if (!found)
        {
            return requiredExtension;
        }
    }
    return std::nullopt;
}

std::optional<VkSurfaceFormatKHR>
SelectSurfaceFormat(const std::span<const VkSurfaceFormatKHR> formats) noexcept
{
    if (formats.empty())
    {
        return std::nullopt;
    }

    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED)
    {
        return VkSurfaceFormatKHR{
            .format = VK_FORMAT_B8G8R8A8_SRGB,
            .colorSpace = formats.front().colorSpace,
        };
    }

    const auto preferred =
        std::ranges::find_if(formats,
                             [](const VkSurfaceFormatKHR& format)
                             {
                                 return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                                        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                             });
    if (preferred != formats.end())
    {
        return *preferred;
    }

    const auto srgb =
        std::ranges::find_if(formats,
                             [](const VkSurfaceFormatKHR& format)
                             {
                                 return IsSrgbFormat(format.format) &&
                                        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                             });
    if (srgb != formats.end())
    {
        return *srgb;
    }

    return formats.front();
}

std::optional<VkPresentModeKHR>
SelectPresentMode(const std::span<const VkPresentModeKHR> presentModes) noexcept
{
    if (presentModes.empty())
    {
        return std::nullopt;
    }

    if (std::ranges::find(presentModes, VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end())
    {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    if (std::ranges::find(presentModes, VK_PRESENT_MODE_FIFO_KHR) != presentModes.end())
    {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    return presentModes.front();
}

VkExtent2D SelectSurfaceExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                               const VkExtent2D requestedExtent) noexcept
{
    constexpr std::uint32_t VariableExtent = std::numeric_limits<std::uint32_t>::max();
    if (capabilities.currentExtent.width != VariableExtent &&
        capabilities.currentExtent.height != VariableExtent)
    {
        return capabilities.currentExtent;
    }

    return {
        .width = std::clamp(requestedExtent.width, capabilities.minImageExtent.width,
                            capabilities.maxImageExtent.width),
        .height = std::clamp(requestedExtent.height, capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height),
    };
}

std::optional<DeviceEvaluation>
EvaluateDeviceSuitability(const DeviceCandidateCapabilities& candidate,
                          std::string& rejectionReason)
{
    rejectionReason.clear();
    if (candidate.apiVersion < RequiredApiVersion)
    {
        rejectionReason =
            "requires Vulkan 1.3; candidate reports " + ApiVersionString(candidate.apiVersion);
        return std::nullopt;
    }

    if (const auto missingExtension = FindMissingRequiredDeviceExtension(candidate.extensions))
    {
        rejectionReason = "missing required device extension " + std::string{*missingExtension};
        return std::nullopt;
    }

    const QueueFamilySelection queueFamilies = SelectQueueFamilies(candidate.queueFamilies);
    if (!queueFamilies.graphicsFamily.has_value())
    {
        rejectionReason = "missing a queue family with graphics support and queueCount > 0";
        return std::nullopt;
    }
    if (!queueFamilies.presentFamily.has_value())
    {
        rejectionReason = "missing a queue family with present support and queueCount > 0";
        return std::nullopt;
    }

    const auto surfaceFormat = SelectSurfaceFormat(candidate.surfaceFormats);
    if (!surfaceFormat.has_value())
    {
        rejectionReason = "surface has no advertised surface formats";
        return std::nullopt;
    }

    const auto presentMode = SelectPresentMode(candidate.presentModes);
    if (!presentMode.has_value())
    {
        rejectionReason = "surface has no advertised present modes";
        return std::nullopt;
    }

    if (!candidate.dynamicRendering)
    {
        rejectionReason = "missing Vulkan 1.3 feature dynamicRendering";
        return std::nullopt;
    }
    if (!candidate.synchronization2)
    {
        rejectionReason = "missing Vulkan 1.3 feature synchronization2";
        return std::nullopt;
    }

    return DeviceEvaluation{
        .queueFamilies = queueFamilies,
        .surfaceFormat = *surfaceFormat,
        .presentMode = *presentMode,
    };
}
} // namespace detail

std::optional<VulkanDeviceSelection> VulkanDeviceSelection::Select(const VkInstance instance,
                                                                   const VkSurfaceKHR surface,
                                                                   std::string& error)
{
    if (instance == VK_NULL_HANDLE)
    {
        error = "Cannot select a Vulkan physical device without a valid VkInstance";
        return std::nullopt;
    }
    if (surface == VK_NULL_HANDLE)
    {
        error = "Cannot select a Vulkan physical device without a valid VkSurfaceKHR";
        return std::nullopt;
    }

    std::vector<VkPhysicalDevice> physicalDevices;
    const auto enumeratePhysicalDevices =
        [instance](std::uint32_t* count, VkPhysicalDevice* devices)
    { return vkEnumeratePhysicalDevices(instance, count, devices); };
    if (!EnumerateVulkanValues("vkEnumeratePhysicalDevices", enumeratePhysicalDevices,
                               physicalDevices, error))
    {
        return std::nullopt;
    }
    if (physicalDevices.empty())
    {
        error = "vkEnumeratePhysicalDevices returned no physical devices";
        return std::nullopt;
    }

    std::vector<std::string> rejectionSummaries;
    rejectionSummaries.reserve(physicalDevices.size());
    for (const VkPhysicalDevice physicalDevice : physicalDevices)
    {
        EnumeratedCandidate candidate;
        std::string queryError;
        if (!QueryCandidate(physicalDevice, surface, candidate, queryError))
        {
            const std::string rejection = "Physical device '" + DeviceName(candidate.properties) +
                                          "' query failed: " + queryError;
            owl::foundation::LogMessage(owl::foundation::LogLevel::Warning, "Vulkan", rejection);
            error = rejection;
            return std::nullopt;
        }

        const detail::DeviceCandidateCapabilities capabilities{
            .apiVersion = candidate.properties.apiVersion,
            .extensions = candidate.extensions,
            .queueFamilies = candidate.queueFamilies,
            .surfaceFormats = candidate.surfaceFormats,
            .presentModes = candidate.presentModes,
            .dynamicRendering = candidate.dynamicRendering,
            .synchronization2 = candidate.synchronization2,
        };
        std::string rejectionReason;
        auto evaluation = detail::EvaluateDeviceSuitability(capabilities, rejectionReason);
        if (!evaluation.has_value())
        {
            const std::string rejection = "Physical device '" + DeviceName(candidate.properties) +
                                          "' rejected: " + rejectionReason;
            owl::foundation::LogMessage(owl::foundation::LogLevel::Warning, "Vulkan", rejection);
            rejectionSummaries.push_back(rejection);
            continue;
        }

        owl::foundation::LogMessage(owl::foundation::LogLevel::Info, "Vulkan",
                                    SelectedDeviceMessage(candidate.properties, *evaluation));
        error.clear();
        return VulkanDeviceSelection{physicalDevice, candidate.properties, std::move(*evaluation)};
    }

    error = "No suitable Vulkan physical device found";
    if (!rejectionSummaries.empty())
    {
        error += ". Rejections: ";
        for (std::size_t index = 0; index < rejectionSummaries.size(); ++index)
        {
            if (index > 0)
            {
                error += "; ";
            }
            error += rejectionSummaries[index];
        }
    }
    return std::nullopt;
}

VkPhysicalDevice VulkanDeviceSelection::Get() const noexcept
{
    return physicalDevice_;
}

const VkPhysicalDeviceProperties& VulkanDeviceSelection::Properties() const noexcept
{
    return properties_;
}

const detail::QueueFamilySelection& VulkanDeviceSelection::QueueFamilies() const noexcept
{
    return evaluation_.queueFamilies;
}

VkSurfaceFormatKHR VulkanDeviceSelection::SurfaceFormat() const noexcept
{
    return evaluation_.surfaceFormat;
}

VkPresentModeKHR VulkanDeviceSelection::PresentMode() const noexcept
{
    return evaluation_.presentMode;
}

VulkanDeviceSelection::VulkanDeviceSelection(const VkPhysicalDevice physicalDevice,
                                             const VkPhysicalDeviceProperties& properties,
                                             detail::DeviceEvaluation evaluation) noexcept
    : physicalDevice_(physicalDevice), properties_(properties), evaluation_(std::move(evaluation))
{
}
} // namespace owl::vulkan
