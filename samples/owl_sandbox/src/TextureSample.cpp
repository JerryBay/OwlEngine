#include "TextureSample.h"
#include "VulkanSample.h"

#include <utility>

namespace owl::sandbox
{
    int RunTextureSample()
    {
        owl::vulkan::VulkanTriangleOptions options;
        options.textureShaders = owl::vulkan::TriangleShaderPaths{
            .vertex = "assets/m2/texture.vert.spv",
            .fragment = "assets/m2/texture.frag.spv",
        };
        return RunVulkanSample("Texture", std::move(options));
    }
} // namespace owl::sandbox
