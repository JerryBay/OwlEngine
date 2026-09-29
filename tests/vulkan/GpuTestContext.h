#pragma once

#include "VulkanAllocator.h"
#include "VulkanDevice.h"
#include "VulkanDeviceSelection.h"
#include "VulkanInstance.h"
#include "VulkanSurface.h"

#include <owl/platform/Platform.h>

#include <optional>
#include <string>

namespace owl::tests
{
    // Declaration order keeps native children alive only while their parents remain valid.
    struct GpuContext
    {
        std::optional<owl::platform::Platform> platform;
        std::optional<owl::platform::Window> window;
        std::optional<vulkan::VulkanInstance> instance;
        std::optional<vulkan::VulkanSurface> surface;
        std::optional<vulkan::VulkanDeviceSelection> selection;
        std::optional<vulkan::VulkanDevice> device;
        std::optional<vulkan::VulkanAllocator> allocator;

        GpuContext() = default;
        GpuContext(const GpuContext&) = delete;
        GpuContext& operator=(const GpuContext&) = delete;
        GpuContext(GpuContext&&) noexcept = default;
        // Memberwise assignment would replace parents before releasing their old children.
        GpuContext& operator=(GpuContext&&) = delete;
        ~GpuContext();
    };

    [[nodiscard]] std::optional<GpuContext> CreateGpuContext(std::string& error);
    [[nodiscard]] bool IsGpuTestEnabled();
} // namespace owl::tests
