#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace owl::vulkan
{
namespace detail
{
struct QueueFamilyInfo
{
    std::uint32_t queueCount = 0;
    VkQueueFlags flags = 0;
    bool presentSupported = false;
};

struct QueueFamilySelection
{
    std::optional<std::uint32_t> graphicsFamily;
    std::optional<std::uint32_t> presentFamily;

    [[nodiscard]] bool IsComplete() const noexcept;
    [[nodiscard]] bool UsesUnifiedFamily() const noexcept;
};

struct DeviceCandidateCapabilities
{
    std::uint32_t apiVersion = VK_API_VERSION_1_0;
    std::span<const VkExtensionProperties> extensions;
    std::span<const QueueFamilyInfo> queueFamilies;
    std::span<const VkSurfaceFormatKHR> surfaceFormats;
    std::span<const VkPresentModeKHR> presentModes;
    bool dynamicRendering = false;
    bool synchronization2 = false;
};

struct DeviceEvaluation
{
    QueueFamilySelection queueFamilies;
    VkSurfaceFormatKHR surfaceFormat{};
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
};

[[nodiscard]] QueueFamilySelection
SelectQueueFamilies(std::span<const QueueFamilyInfo> queueFamilies) noexcept;

[[nodiscard]] bool
HasReportedSurfaceSupport(std::span<const QueueFamilyInfo> queueFamilies) noexcept;

[[nodiscard]] std::optional<std::string_view>
FindMissingRequiredDeviceExtension(std::span<const VkExtensionProperties> extensions) noexcept;

[[nodiscard]] std::optional<VkSurfaceFormatKHR>
SelectSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats) noexcept;

[[nodiscard]] std::optional<VkPresentModeKHR>
SelectPresentMode(std::span<const VkPresentModeKHR> presentModes) noexcept;

[[nodiscard]] VkExtent2D SelectSurfaceExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                             VkExtent2D requestedExtent) noexcept;

[[nodiscard]] std::optional<DeviceEvaluation>
EvaluateDeviceSuitability(const DeviceCandidateCapabilities& candidate,
                          std::string& rejectionReason);
} // namespace detail

// A non-owning capability snapshot. The VkPhysicalDevice remains owned by its VkInstance.
class VulkanDeviceSelection
{
  public:
    [[nodiscard]] static std::optional<VulkanDeviceSelection>
    Select(VkInstance instance, VkSurfaceKHR surface, std::string& error);

    [[nodiscard]] VkPhysicalDevice Get() const noexcept;
    [[nodiscard]] const VkPhysicalDeviceProperties& Properties() const noexcept;
    [[nodiscard]] const detail::QueueFamilySelection& QueueFamilies() const noexcept;
    [[nodiscard]] VkSurfaceFormatKHR SurfaceFormat() const noexcept;
    [[nodiscard]] VkPresentModeKHR PresentMode() const noexcept;

  private:
    VulkanDeviceSelection(VkPhysicalDevice physicalDevice,
                          const VkPhysicalDeviceProperties& properties,
                          detail::DeviceEvaluation evaluation) noexcept;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties_{};
    detail::DeviceEvaluation evaluation_{};
};
} // namespace owl::vulkan
