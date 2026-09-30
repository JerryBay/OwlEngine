#include "TriangleSample.h"
#include "VulkanSample.h"

#include <utility>

namespace owl::sandbox
{
    int RunTriangleSample()
    {
        owl::vulkan::VulkanTriangleOptions options;
        options.triangleShaders = owl::vulkan::TriangleShaderPaths{
            .vertex = "assets/m1/triangle.vert.spv",
            .fragment = "assets/m1/triangle.frag.spv",
        };
        return RunVulkanSample("Triangle", std::move(options));
    }
} // namespace owl::sandbox
