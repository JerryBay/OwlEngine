#include "VulkanSwapchain.h"

#include "VulkanDevice.h"
#include "VulkanSurface.h"

#include <owl/foundation/Log.h>

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace owl::vulkan
{
    namespace
    {
        SwapchainUpdateResult FailVulkan(const std::string_view operation, const VkResult result,
                                         std::string& error)
        {
            error = std::string{operation} + " failed with VkResult " +
                    std::to_string(static_cast<int>(result));
            owl::foundation::LogMessage(owl::foundation::LogLevel::Error, "Vulkan", error);
            return SwapchainUpdateResult::Failed;
        }

        template <typename Value, typename Enumerate>
        VkResult EnumerateValues(Enumerate&& enumerate, std::vector<Value>& values)
        {
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                std::uint32_t count = 0;
                VkResult result = enumerate(&count, nullptr);
                if (result != VK_SUCCESS && result != VK_INCOMPLETE)
                {
                    return result;
                }
                if (result == VK_INCOMPLETE)
                {
                    continue;
                }
                values.resize(count);
                if (count == 0)
                {
                    return VK_SUCCESS;
                }
                result = enumerate(&count, values.data());
                if (result == VK_SUCCESS)
                {
                    values.resize(count);
                    return VK_SUCCESS;
                }
                if (result != VK_INCOMPLETE)
                {
                    return result;
                }
            }
            return VK_INCOMPLETE;
        }
    } // namespace

    namespace detail
    {
        SwapchainUpdateResult
        SelectSwapchainConfiguration(const SwapchainSupport& support,
                                     const QueueFamilySelection& queueFamilies,
                                     const VkExtent2D requestedExtent,
                                     SwapchainConfiguration& configuration, std::string& error)
        {
            configuration = {};
            error.clear();
            const auto& capabilities = support.capabilities;
            if (requestedExtent.width == 0 || requestedExtent.height == 0 ||
                capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0 ||
                capabilities.maxImageExtent.width == 0 || capabilities.maxImageExtent.height == 0)
            {
                return SwapchainUpdateResult::Deferred;
            }
            if (!queueFamilies.IsComplete())
            {
                error = "Swapchain requires graphics and present queue families";
                return SwapchainUpdateResult::Failed;
            }
            if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
            {
                error = "Surface does not support COLOR_ATTACHMENT swapchain images";
                return SwapchainUpdateResult::Failed;
            }
            if (capabilities.minImageExtent.width > capabilities.maxImageExtent.width ||
                capabilities.minImageExtent.height > capabilities.maxImageExtent.height ||
                capabilities.maxImageArrayLayers < 1 ||
                (capabilities.maxImageCount != 0 &&
                 capabilities.maxImageCount < capabilities.minImageCount))
            {
                error = "Surface reports inconsistent swapchain limits";
                return SwapchainUpdateResult::Failed;
            }
            if ((capabilities.supportedTransforms & capabilities.currentTransform) == 0)
            {
                error = "Surface current transform is unsupported";
                return SwapchainUpdateResult::Failed;
            }

            SwapchainConfiguration selected;
            constexpr std::array alphaPreferences{
                VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
            for (const auto alpha : alphaPreferences)
            {
                if ((capabilities.supportedCompositeAlpha & alpha) != 0)
                {
                    selected.compositeAlpha = alpha;
                    break;
                }
            }
            if (selected.compositeAlpha == 0)
            {
                error = "Surface has no supported composite alpha mode";
                return SwapchainUpdateResult::Failed;
            }
            const auto format = SelectSurfaceFormat(support.formats);
            if (!format.has_value())
            {
                error = "Surface has no usable swapchain format";
                return SwapchainUpdateResult::Failed;
            }
            const auto mode = SelectPresentMode(support.presentModes);
            if (!mode.has_value())
            {
                error = "Surface has no usable present mode";
                return SwapchainUpdateResult::Failed;
            }
            selected.surfaceFormat = *format;
            selected.presentMode = *mode;
            selected.extent = SelectSurfaceExtent(capabilities, requestedExtent);
            selected.minImageCount = capabilities.minImageCount;
            if (selected.minImageCount < std::numeric_limits<std::uint32_t>::max())
            {
                ++selected.minImageCount;
            }
            if (capabilities.maxImageCount != 0)
            {
                selected.minImageCount =
                    std::min(selected.minImageCount, capabilities.maxImageCount);
            }
            selected.preTransform = capabilities.currentTransform;
            if (!queueFamilies.UsesUnifiedFamily())
            {
                selected.sharingMode = VK_SHARING_MODE_CONCURRENT;
                selected.queueFamilyIndices = {*queueFamilies.graphicsFamily,
                                               *queueFamilies.presentFamily};
                selected.queueFamilyIndexCount = 2;
            }
            configuration = selected;
            return SwapchainUpdateResult::Ready;
        }
    } // namespace detail

    VulkanSwapchain::~VulkanSwapchain()
    {
        Reset();
    }

    VulkanSwapchain::VulkanSwapchain(VulkanSwapchain&& other) noexcept
        : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
          surface_(std::exchange(other.surface_, VK_NULL_HANDLE)),
          swapchain_(std::exchange(other.swapchain_, VK_NULL_HANDLE)),
          configuration_(std::exchange(other.configuration_, detail::SwapchainConfiguration{})),
          images_(std::move(other.images_)), imageViews_(std::move(other.imageViews_))
    {
        other.images_.clear();
        other.imageViews_.clear();
    }

    VulkanSwapchain& VulkanSwapchain::operator=(VulkanSwapchain&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            surface_ = std::exchange(other.surface_, VK_NULL_HANDLE);
            swapchain_ = std::exchange(other.swapchain_, VK_NULL_HANDLE);
            configuration_ = std::exchange(other.configuration_, detail::SwapchainConfiguration{});
            images_ = std::move(other.images_);
            imageViews_ = std::move(other.imageViews_);
            other.images_.clear();
            other.imageViews_.clear();
        }
        return *this;
    }

    SwapchainUpdateResult VulkanSwapchain::CreateOrRecreate(const VulkanDevice& device,
                                                            const VulkanSurface& surface,
                                                            const VkExtent2D requestedExtent,
                                                            std::string& error)
    {
        if (!device.IsValid() || device.PhysicalDevice() == VK_NULL_HANDLE ||
            !device.QueueFamilies().IsComplete())
        {
            error = "Cannot create a swapchain without a valid Vulkan device and queues";
            return SwapchainUpdateResult::Failed;
        }
        if (!surface.IsValid())
        {
            error = "Cannot create a swapchain without a valid Vulkan surface";
            return SwapchainUpdateResult::Failed;
        }
        if (IsValid() && (device_ != device.Get() || surface_ != surface.Get()))
        {
            error = "Cannot change the device or surface of a live swapchain owner";
            return SwapchainUpdateResult::Failed;
        }
        error.clear();
        if (requestedExtent.width == 0 || requestedExtent.height == 0)
        {
            return SwapchainUpdateResult::Deferred;
        }

        VkBool32 canPresent = VK_FALSE;
        VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
            device.PhysicalDevice(), *device.QueueFamilies().presentFamily, surface.Get(),
            &canPresent);
        if (result != VK_SUCCESS)
        {
            return FailVulkan("vkGetPhysicalDeviceSurfaceSupportKHR", result, error);
        }
        if (canPresent != VK_TRUE)
        {
            error = "Device present queue does not support this surface";
            return SwapchainUpdateResult::Failed;
        }

        VkSurfaceCapabilitiesKHR capabilities{};
        result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.PhysicalDevice(), surface.Get(),
                                                           &capabilities);
        if (result != VK_SUCCESS)
        {
            return FailVulkan("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result, error);
        }
        if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0 ||
            capabilities.maxImageExtent.width == 0 || capabilities.maxImageExtent.height == 0)
        {
            return SwapchainUpdateResult::Deferred;
        }
        std::vector<VkSurfaceFormatKHR> formats;
        result = EnumerateValues(
            [&](std::uint32_t* count, VkSurfaceFormatKHR* values)
            {
                return vkGetPhysicalDeviceSurfaceFormatsKHR(device.PhysicalDevice(), surface.Get(),
                                                            count, values);
            },
            formats);
        if (result != VK_SUCCESS)
        {
            return FailVulkan("vkGetPhysicalDeviceSurfaceFormatsKHR", result, error);
        }
        std::vector<VkPresentModeKHR> presentModes;
        result = EnumerateValues(
            [&](std::uint32_t* count, VkPresentModeKHR* values)
            {
                return vkGetPhysicalDeviceSurfacePresentModesKHR(device.PhysicalDevice(),
                                                                 surface.Get(), count, values);
            },
            presentModes);
        if (result != VK_SUCCESS)
        {
            return FailVulkan("vkGetPhysicalDeviceSurfacePresentModesKHR", result, error);
        }
        detail::SwapchainConfiguration configuration;
        const auto selected = detail::SelectSwapchainConfiguration(
            {capabilities, formats, presentModes}, device.QueueFamilies(), requestedExtent,
            configuration, error);
        if (selected != SwapchainUpdateResult::Ready)
        {
            return selected;
        }

        const VkSwapchainCreateInfoKHR createInfo{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface.Get(),
            .minImageCount = configuration.minImageCount,
            .imageFormat = configuration.surfaceFormat.format,
            .imageColorSpace = configuration.surfaceFormat.colorSpace,
            .imageExtent = configuration.extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = configuration.sharingMode,
            .queueFamilyIndexCount = configuration.queueFamilyIndexCount,
            .pQueueFamilyIndices = configuration.queueFamilyIndexCount == 0
                                       ? nullptr
                                       : configuration.queueFamilyIndices.data(),
            .preTransform = configuration.preTransform,
            .compositeAlpha = configuration.compositeAlpha,
            .presentMode = configuration.presentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = swapchain_,
        };
        VkSwapchainKHR handle = VK_NULL_HANDLE;
        result = vkCreateSwapchainKHR(device.Get(), &createInfo, nullptr, &handle);
        // The call retires oldSwapchain even on failure. Never expose it as usable again.
        Reset();
        if (result != VK_SUCCESS)
        {
            return FailVulkan("vkCreateSwapchainKHR", result, error);
        }

        VulkanSwapchain candidate;
        candidate.device_ = device.Get();
        candidate.surface_ = surface.Get();
        candidate.swapchain_ = handle;
        candidate.configuration_ = configuration;
        result = EnumerateValues(
            [&](std::uint32_t* count, VkImage* values)
            {
                return vkGetSwapchainImagesKHR(candidate.device_, candidate.swapchain_, count,
                                               values);
            },
            candidate.images_);
        if (result != VK_SUCCESS)
        {
            return FailVulkan("vkGetSwapchainImagesKHR", result, error);
        }
        if (candidate.images_.empty())
        {
            error = "vkGetSwapchainImagesKHR returned no images";
            return SwapchainUpdateResult::Failed;
        }
        candidate.imageViews_.reserve(candidate.images_.size());
        for (const VkImage image : candidate.images_)
        {
            const VkImageViewCreateInfo viewInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = configuration.surfaceFormat.format,
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            VkImageView view = VK_NULL_HANDLE;
            result = vkCreateImageView(candidate.device_, &viewInfo, nullptr, &view);
            if (result != VK_SUCCESS)
            {
                return FailVulkan("vkCreateImageView", result, error);
            }
            candidate.imageViews_.push_back(view);
        }
        owl::foundation::LogMessage(
            owl::foundation::LogLevel::Info, "Vulkan",
            "Created swapchain (extent=" + std::to_string(configuration.extent.width) + "x" +
                std::to_string(configuration.extent.height) +
                ", images=" + std::to_string(candidate.images_.size()) +
                ", format=" + std::to_string(configuration.surfaceFormat.format) +
                ", presentMode=" + std::to_string(configuration.presentMode) +
                ", sharingMode=" + std::to_string(configuration.sharingMode) + ")");
        *this = std::move(candidate);
        return SwapchainUpdateResult::Ready;
    }

    bool VulkanSwapchain::IsValid() const noexcept
    {
        return swapchain_ != VK_NULL_HANDLE;
    }
    VkSwapchainKHR VulkanSwapchain::Get() const noexcept
    {
        return swapchain_;
    }
    VkExtent2D VulkanSwapchain::Extent() const noexcept
    {
        return configuration_.extent;
    }
    VkSurfaceFormatKHR VulkanSwapchain::SurfaceFormat() const noexcept
    {
        return configuration_.surfaceFormat;
    }
    VkPresentModeKHR VulkanSwapchain::PresentMode() const noexcept
    {
        return configuration_.presentMode;
    }
    std::span<const VkImage> VulkanSwapchain::Images() const noexcept
    {
        return images_;
    }
    std::span<const VkImageView> VulkanSwapchain::ImageViews() const noexcept
    {
        return imageViews_;
    }

    void VulkanSwapchain::Reset() noexcept
    {
        for (const VkImageView view : imageViews_)
        {
            vkDestroyImageView(device_, view, nullptr);
        }
        if (swapchain_ != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        }
        imageViews_.clear();
        images_.clear();
        device_ = VK_NULL_HANDLE;
        surface_ = VK_NULL_HANDLE;
        swapchain_ = VK_NULL_HANDLE;
        configuration_ = {};
    }
} // namespace owl::vulkan
