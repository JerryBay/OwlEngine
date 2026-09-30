#pragma once

#include <owl/vulkan/VulkanTriangle.h>

#include <string_view>

namespace owl::sandbox
{
    // Shader asset paths are relative to the executable; no shaders selects the clear sample.
    [[nodiscard]] int RunVulkanSample(std::string_view name,
                                      owl::vulkan::VulkanTriangleOptions options = {});
} // namespace owl::sandbox
