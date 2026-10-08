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

    int RunMipTextureSample(const bool forceCpu)
    {
        owl::vulkan::VulkanTriangleOptions options;
        options.textureShaders = owl::vulkan::TriangleShaderPaths{
            .vertex = "assets/m2/texture_mips.vert.spv",
            .fragment = "assets/m2/texture_mips.frag.spv",
        };
        options.textureMipMode = forceCpu ? owl::vulkan::TextureMipMode::ForceCpu
                                          : owl::vulkan::TextureMipMode::Generate;
        return RunVulkanSample("Texture Mips", std::move(options));
    }
} // namespace owl::sandbox
