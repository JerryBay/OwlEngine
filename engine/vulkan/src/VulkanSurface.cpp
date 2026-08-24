#include "VulkanSurface.h"

#include "SDLWindowAccess.h"
#include "VulkanInstance.h"

#include <owl/platform/Window.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

#include <utility>

namespace owl::vulkan
{
    VulkanSurface::~VulkanSurface()
    {
        Reset();
    }

    VulkanSurface::VulkanSurface(VulkanSurface&& other) noexcept
        : instance_(std::exchange(other.instance_, VK_NULL_HANDLE)),
          surface_(std::exchange(other.surface_, VK_NULL_HANDLE))
    {
    }

    VulkanSurface& VulkanSurface::operator=(VulkanSurface&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            instance_ = std::exchange(other.instance_, VK_NULL_HANDLE);
            surface_ = std::exchange(other.surface_, VK_NULL_HANDLE);
        }
        return *this;
    }

    std::optional<VulkanSurface> VulkanSurface::Create(const VulkanInstance& instance,
                                                       owl::platform::Window& window,
                                                       std::string& error)
    {
        if (!instance.IsValid())
        {
            error = "Cannot create a Vulkan surface without a valid VkInstance";
            return std::nullopt;
        }

        SDL_Window* sdlWindow = owl::platform::SDLWindowAccess::Get(window);
        if (sdlWindow == nullptr)
        {
            error = "Cannot create a Vulkan surface for an invalid Platform window";
            return std::nullopt;
        }

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(sdlWindow, instance.Get(), nullptr, &surface))
        {
            error = "SDL_Vulkan_CreateSurface failed: " + std::string{SDL_GetError()};
            return std::nullopt;
        }

        error.clear();
        return VulkanSurface{instance.Get(), surface};
    }

    bool VulkanSurface::IsValid() const noexcept
    {
        return surface_ != VK_NULL_HANDLE;
    }

    VkSurfaceKHR VulkanSurface::Get() const noexcept
    {
        return surface_;
    }

    VulkanSurface::VulkanSurface(const VkInstance instance, const VkSurfaceKHR surface) noexcept
        : instance_(instance), surface_(surface)
    {
    }

    void VulkanSurface::Reset() noexcept
    {
        if (instance_ != VK_NULL_HANDLE && surface_ != VK_NULL_HANDLE)
        {
            SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
        }

        instance_ = VK_NULL_HANDLE;
        surface_ = VK_NULL_HANDLE;
    }
} // namespace owl::vulkan
