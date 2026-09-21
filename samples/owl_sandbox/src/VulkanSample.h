#pragma once

#include <owl/vulkan/VulkanTriangle.h>

#include <optional>
#include <string_view>

namespace owl::sandbox
{
    // Shader asset paths are relative to the executable; no shaders selects the clear sample.
    [[nodiscard]] int
    RunVulkanSample(std::string_view name,
                    std::optional<owl::vulkan::TriangleShaderPaths> shaderAssets = std::nullopt);
} // namespace owl::sandbox
