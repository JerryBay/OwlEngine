#include "GpuTestContext.h"

#include <SDL3/SDL_stdinc.h>

#include <string_view>
#include <utility>

namespace owl::tests
{
    GpuContext::~GpuContext()
    {
        if (device && device->IsValid())
            static_cast<void>(vkDeviceWaitIdle(device->Get()));
    }

    std::optional<GpuContext> CreateGpuContext(std::string& error)
    {
        GpuContext result;
        result.platform = owl::platform::Platform::Create(error);
        if (!result.platform)
            return std::nullopt;
        const owl::platform::WindowDesc windowDesc{
            .title = "OwlEngine - Vulkan Resource Test",
            .width = 320,
            .height = 180,
            .resizable = false,
            .surfaceApi = owl::platform::WindowSurfaceApi::Vulkan,
        };
        result.window = result.platform->CreateWindow(windowDesc, error);
        if (!result.window)
            return std::nullopt;
        result.instance = owl::vulkan::VulkanInstance::Create(error);
        if (!result.instance)
            return std::nullopt;
        result.surface = owl::vulkan::VulkanSurface::Create(*result.instance, *result.window, error);
        if (!result.surface)
            return std::nullopt;
        result.selection = owl::vulkan::VulkanDeviceSelection::Select(
            result.instance->Get(), result.surface->Get(), error);
        if (!result.selection)
            return std::nullopt;
        result.device = owl::vulkan::VulkanDevice::Create(
            *result.selection, error, result.instance->PresentationSupport());
        if (!result.device)
            return std::nullopt;
        result.allocator = owl::vulkan::VulkanAllocator::Create(
            result.instance->Get(), *result.device, error);
        if (!result.allocator)
            return std::nullopt;
        error.clear();
        return std::optional<GpuContext>{std::move(result)};
    }

    bool IsGpuTestEnabled()
    {
        const char* enabled = SDL_getenv("OWL_RUN_VULKAN_BOOTSTRAP_TEST");
        return enabled != nullptr && std::string_view{enabled} == "1";
    }
} // namespace owl::tests
