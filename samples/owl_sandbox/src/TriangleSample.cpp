#include "TriangleSample.h"
#include "VulkanSample.h"

namespace owl::sandbox
{
    int RunTriangleSample()
    {
        const owl::vulkan::TriangleShaderPaths shaderAssets{
            .vertex = "assets/m1/triangle.vert.spv",
            .fragment = "assets/m1/triangle.frag.spv",
        };
        return RunVulkanSample("Triangle", shaderAssets);
    }
} // namespace owl::sandbox
