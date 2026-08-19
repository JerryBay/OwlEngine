#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <string>

namespace owl::platform
{
class Window;
}

namespace owl::vulkan
{
class VulkanInstance;

class VulkanSurface
{
  public:
    VulkanSurface() noexcept = default;
    ~VulkanSurface();

    VulkanSurface(const VulkanSurface&) = delete;
    VulkanSurface& operator=(const VulkanSurface&) = delete;

    VulkanSurface(VulkanSurface&& other) noexcept;
    VulkanSurface& operator=(VulkanSurface&& other) noexcept;

    [[nodiscard]] static std::optional<VulkanSurface>
    Create(const VulkanInstance& instance, owl::platform::Window& window, std::string& error);

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] VkSurfaceKHR Get() const noexcept;

  private:
    VulkanSurface(VkInstance instance, VkSurfaceKHR surface) noexcept;
    void Reset() noexcept;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
};
} // namespace owl::vulkan
