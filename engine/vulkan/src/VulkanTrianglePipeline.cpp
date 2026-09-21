#include "VulkanTrianglePipeline.h"

#include "VulkanDevice.h"
#include <owl/foundation/Log.h>
#include <owl/vulkan/VulkanTriangle.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <utility>

namespace owl::vulkan::detail
{
    std::optional<std::uint32_t>
    SelectTriangleMemoryType(const VkPhysicalDeviceMemoryProperties& properties,
                             const std::uint32_t memoryTypeBits) noexcept
    {
        std::optional<std::uint32_t> fallback;
        for (std::uint32_t index = 0; index < properties.memoryTypeCount && index < 32; ++index)
        {
            const auto flags = properties.memoryTypes[index].propertyFlags;
            if ((memoryTypeBits & (1U << index)) == 0 ||
                (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
                continue;
            if ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0)
                return index;
            if (!fallback)
                fallback = index;
        }
        return fallback;
    }

    std::optional<std::vector<std::uint32_t>> ReadTriangleSpirv(const std::filesystem::path& path,
                                                                std::string& error)
    {
        const auto utf8 = path.u8string();
        const std::string name{utf8.begin(), utf8.end()};
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file)
        {
            error = "Cannot open triangle SPIR-V: " + name;
            return std::nullopt;
        }
        const auto bytes = file.tellg();
        // Bound this sample's inputs and allocate uint32_t storage for pCode alignment.
        if (bytes < 20 || bytes > 4 * 1024 * 1024 || bytes % 4 != 0)
        {
            error = "Invalid triangle SPIR-V byte size: " + name;
            return std::nullopt;
        }
        std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes) / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(bytes));
        if (!file || words[0] != 0x07230203 || words[1] < 0x00010000 || words[1] > 0x00010600 ||
            words[3] == 0 || words[4] != 0)
        {
            error = "Invalid triangle SPIR-V header or incomplete read: " + name;
            return std::nullopt;
        }
        error.clear();
        return words;
    }
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    namespace
    {
        struct Vertex
        {
            float position[2];
            float color[3];
        };
        constexpr std::array<Vertex, 3> Vertices{{
            {{0.0F, -0.6F}, {1.0F, 0.0F, 0.0F}},
            {{0.6F, 0.6F}, {0.0F, 1.0F, 0.0F}},
            {{-0.6F, 0.6F}, {0.0F, 0.0F, 1.0F}},
        }};
        constexpr std::array<std::uint16_t, 3> Indices{0, 1, 2};
        constexpr VkDeviceSize IndexOffset = sizeof(Vertices);
        static_assert(sizeof(Vertex) == 5 * sizeof(float));
        static_assert(IndexOffset % alignof(std::uint16_t) == 0);

        bool Check(const VkResult result, const char* operation, std::string& error)
        {
            if (result == VK_SUCCESS)
                return true;
            error = std::string{operation} + " failed with VkResult " +
                    std::to_string(static_cast<int>(result));
            owl::foundation::LogMessage(owl::foundation::LogLevel::Error, "Vulkan", error);
            return false;
        }

        struct ShaderModules
        {
            VkDevice device;
            VkShaderModule vertex = VK_NULL_HANDLE;
            VkShaderModule fragment = VK_NULL_HANDLE;
            ~ShaderModules()
            {
                vkDestroyShaderModule(device, fragment, nullptr);
                vkDestroyShaderModule(device, vertex, nullptr);
            }
        };

        bool CreateShader(const VkDevice device, const std::vector<std::uint32_t>& words,
                          VkShaderModule& destination, std::string& error)
        {
            const VkShaderModuleCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = words.size() * sizeof(std::uint32_t),
                .pCode = words.data(),
            };
            VkShaderModule shader = VK_NULL_HANDLE;
            if (!Check(vkCreateShaderModule(device, &info, nullptr, &shader),
                       "vkCreateShaderModule", error))
                return false;
            destination = shader;
            return true;
        }
    } // namespace

    VulkanTrianglePipeline::~VulkanTrianglePipeline()
    {
        if (device_ == VK_NULL_HANDLE)
            return;
        vkDestroyPipeline(device_, pipeline_, nullptr);
        vkDestroyPipelineLayout(device_, layout_, nullptr);
        vkDestroyBuffer(device_, geometry_, nullptr);
        vkFreeMemory(device_, memory_, nullptr);
    }

    bool VulkanTrianglePipeline::Initialize(const VulkanDevice& device,
                                            const TriangleShaderPaths& paths, std::string& error)
    {
        if (!device.IsValid() || device_ != VK_NULL_HANDLE)
        {
            error = "Triangle resources require a valid device and an empty owner";
            return false;
        }
        auto vertex = detail::ReadTriangleSpirv(paths.vertex, error);
        if (!vertex)
            return false;
        auto fragment = detail::ReadTriangleSpirv(paths.fragment, error);
        if (!fragment)
            return false;
        vertexShader_ = std::move(*vertex);
        fragmentShader_ = std::move(*fragment);
        device_ = device.Get();

        const VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(Vertices) + sizeof(Indices),
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VkBuffer buffer = VK_NULL_HANDLE;
        if (!Check(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer),
                   "vkCreateBuffer(triangle)", error))
            return false;
        geometry_ = buffer;
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, geometry_, &requirements);
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(device.PhysicalDevice(), &properties);
        const auto memoryType =
            detail::SelectTriangleMemoryType(properties, requirements.memoryTypeBits);
        if (!memoryType)
        {
            error = "Triangle buffer has no compatible host-visible memory type";
            return false;
        }
        const VkMemoryAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = *memoryType,
        };
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (!Check(vkAllocateMemory(device_, &allocate, nullptr, &memory),
                   "vkAllocateMemory(triangle)", error))
            return false;
        memory_ = memory;
        if (!Check(vkBindBufferMemory(device_, geometry_, memory_, 0),
                   "vkBindBufferMemory(triangle)", error))
            return false;
        void* mapped = nullptr;
        if (!Check(vkMapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, &mapped),
                   "vkMapMemory(triangle)", error))
            return false;
        std::memcpy(mapped, Vertices.data(), sizeof(Vertices));
        std::memcpy(static_cast<std::byte*>(mapped) + IndexOffset, Indices.data(), sizeof(Indices));
        const bool coherent = (properties.memoryTypes[*memoryType].propertyFlags &
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        bool flushed = true;
        if (!coherent)
        {
            // Map/flush the entire allocation: offset 0 and WHOLE_SIZE satisfy atom alignment.
            const VkMappedMemoryRange range{
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = memory_,
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            };
            flushed = Check(vkFlushMappedMemoryRanges(device_, 1, &range),
                            "vkFlushMappedMemoryRanges(triangle)", error);
        }
        vkUnmapMemory(device_, memory_);
        if (!flushed)
            return false;
        // Immutable thereafter. Queue submission makes preceding host writes available to the GPU.
        const VkPipelineLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        VkPipelineLayout layout = VK_NULL_HANDLE;
        if (!Check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout),
                   "vkCreatePipelineLayout(triangle)", error))
            return false;
        layout_ = layout;
        owl::foundation::LogMessage(
            owl::foundation::LogLevel::Info, "Vulkan",
            "Triangle geometry uploaded (3 vertices, 3 uint16 indices, memoryType=" +
                std::to_string(*memoryType) + ", coherent=" + (coherent ? "true" : "false") + ")");
        error.clear();
        return true;
    }

    bool VulkanTrianglePipeline::SetColorFormat(const VkFormat format, std::string& error)
    {
        error.clear();
        if (layout_ == VK_NULL_HANDLE || format == VK_FORMAT_UNDEFINED)
        {
            error = "Triangle pipeline requires initialized geometry and a color format";
            return false;
        }
        if (pipeline_ != VK_NULL_HANDLE && format_ == format)
            return true;
        ShaderModules shaders{device_};
        if (!CreateShader(device_, vertexShader_, shaders.vertex, error) ||
            !CreateShader(device_, fragmentShader_, shaders.fragment, error))
            return false;
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = shaders.vertex,
             .pName = "main"},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = shaders.fragment,
             .pName = "main"},
        }};
        const VkVertexInputBindingDescription binding{0, sizeof(Vertex),
                                                      VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription, 2> attributes{{
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
        }};
        const VkPipelineVertexInputStateCreateInfo vertexInput{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &binding,
            .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
            .pVertexAttributeDescriptions = attributes.data(),
        };
        const VkPipelineInputAssemblyStateCreateInfo assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };
        const VkPipelineViewportStateCreateInfo viewport{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };
        const VkPipelineRasterizationStateCreateInfo raster{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0F,
        };
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        const VkPipelineColorBlendAttachmentState colorBlend{
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };
        const VkPipelineColorBlendStateCreateInfo blend{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &colorBlend,
        };
        const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamic{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data(),
        };
        const VkPipelineRenderingCreateInfo rendering{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &format,
        };
        const VkGraphicsPipelineCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering,
            .stageCount = static_cast<std::uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vertexInput,
            .pInputAssemblyState = &assembly,
            .pViewportState = &viewport,
            .pRasterizationState = &raster,
            .pMultisampleState = &multisample,
            .pColorBlendState = &blend,
            .pDynamicState = &dynamic,
            .layout = layout_,
            .renderPass = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (!Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                   "vkCreateGraphicsPipelines(triangle)", error))
        {
            vkDestroyPipeline(device_, pipeline, nullptr);
            return false;
        }
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = pipeline;
        format_ = format;
        owl::foundation::LogMessage(owl::foundation::LogLevel::Info, "Vulkan",
                                    "Triangle graphics pipeline created (colorFormat=" +
                                        std::to_string(static_cast<int>(format)) +
                                        ", dynamic viewport/scissor)");
        return true;
    }

    void VulkanTrianglePipeline::RecordDraw(const VkCommandBuffer command,
                                            const VkExtent2D extent) const noexcept
    {
        const VkViewport viewport{
            0.0F, 0.0F, static_cast<float>(extent.width), static_cast<float>(extent.height),
            0.0F, 1.0F};
        const VkRect2D scissor{{0, 0}, extent};
        const VkDeviceSize vertexOffset = 0;
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);
        vkCmdBindVertexBuffers(command, 0, 1, &geometry_, &vertexOffset);
        vkCmdBindIndexBuffer(command, geometry_, IndexOffset, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(command, static_cast<std::uint32_t>(Indices.size()), 1, 0, 0, 0);
    }
} // namespace owl::vulkan
