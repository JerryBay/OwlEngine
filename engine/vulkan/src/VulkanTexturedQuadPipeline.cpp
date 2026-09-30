#include "VulkanTexturedQuadPipeline.h"

#include "VulkanDevice.h"
#include "VulkanShaderBinary.h"
#include <owl/foundation/Log.h>
#include <owl/vulkan/VulkanTriangle.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <utility>

namespace owl::vulkan::detail
{
    std::array<std::byte, 64 * 64 * 4> MakeCheckerboardRgba8() noexcept
    {
        std::array<std::byte, 64 * 64 * 4> result{};
        constexpr std::array<std::byte, 4> light{std::byte{0xF4}, std::byte{0xF7}, std::byte{0xF9},
                                                 std::byte{0xFF}};
        constexpr std::array<std::byte, 4> dark{std::byte{0x22}, std::byte{0x29}, std::byte{0x32},
                                                std::byte{0xFF}};
        constexpr std::array<std::byte, 4> origin{std::byte{0xE8}, std::byte{0x35}, std::byte{0x35},
                                                  std::byte{0xFF}};
        for (std::size_t y = 0; y < 64; ++y)
        {
            for (std::size_t x = 0; x < 64; ++x)
            {
                const auto& color =
                    x < 8 && y < 8 ? origin : (((x / 8 + y / 8) & 1) == 0 ? light : dark);
                std::memcpy(result.data() + (y * 64 + x) * 4, color.data(), 4);
            }
        }
        return result;
    }
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    namespace
    {
        struct Vertex
        {
            float position[2];
            float uv[2];
        };
        constexpr std::array<Vertex, 4> Vertices{{
            {{-0.78F, -0.78F}, {0.0F, 0.0F}},
            {{0.78F, -0.78F}, {1.0F, 0.0F}},
            {{0.78F, 0.78F}, {1.0F, 1.0F}},
            {{-0.78F, 0.78F}, {0.0F, 1.0F}},
        }};
        constexpr std::array<std::uint16_t, 6> Indices{0, 1, 2, 2, 3, 0};
        constexpr VkDeviceSize IndexOffset = sizeof(Vertices);
        static_assert(sizeof(Vertex) == 4 * sizeof(float));
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
                       "vkCreateShaderModule(texture)", error))
                return false;
            destination = shader;
            return true;
        }
    } // namespace

    VulkanTexturedQuadPipeline::~VulkanTexturedQuadPipeline()
    {
        if (device_ == VK_NULL_HANDLE)
            return;
        vkDestroyPipeline(device_, pipeline_, nullptr);
        vkDestroyPipelineLayout(device_, layout_, nullptr);
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
    }

    bool VulkanTexturedQuadPipeline::Initialize(const VulkanDevice& device,
                                                const VulkanAllocator& allocator,
                                                const TriangleShaderPaths& paths,
                                                std::string& error)
    {
        if (!device.IsValid() || device_ != VK_NULL_HANDLE)
        {
            error = "Textured quad requires a valid device and an empty owner";
            return false;
        }
        auto vertex = detail::ReadSampleSpirv(paths.vertex, error);
        if (!vertex)
            return false;
        auto fragment = detail::ReadSampleSpirv(paths.fragment, error);
        if (!fragment)
            return false;
        vertexShader_ = std::move(*vertex);
        fragmentShader_ = std::move(*fragment);
        device_ = device.Get();

        std::array<std::byte, sizeof(Vertices) + sizeof(Indices)> payload{};
        std::memcpy(payload.data(), Vertices.data(), sizeof(Vertices));
        std::memcpy(payload.data() + IndexOffset, Indices.data(), sizeof(Indices));
        constexpr VkPipelineStageFlags2 geometryStages =
            VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        constexpr VkAccessFlags2 geometryAccess =
            VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
        auto geometryUpload = VulkanBufferUpload::Create(
            device, allocator, std::as_bytes(std::span{payload}),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, geometryStages,
            geometryAccess, error);
        if (!geometryUpload)
            return false;
        geometryUpload_ = std::move(*geometryUpload);
        if (geometryUpload_->SubmitAndWait(error) != VK_SUCCESS ||
            WaitForUpload(error) != VK_SUCCESS)
            return false;

        constexpr VkExtent2D imageExtent{64, 64};
        constexpr VulkanImageDesc imageDesc{
            .extent = imageExtent,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .mipLevels = 1,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        };
        const auto pixels = detail::MakeCheckerboardRgba8();
        auto imageUpload = VulkanImageUpload::Create(device, allocator, imageDesc,
                                                     std::as_bytes(std::span{pixels}), error);
        if (!imageUpload)
            return false;
        imageUpload_ = std::move(*imageUpload);
        if (imageUpload_->Submit(error) != VK_SUCCESS || WaitForUpload(error) != VK_SUCCESS)
            return false;
        view_ = VulkanImageView::Create(*image_, {}, error);
        if (!view_)
            return false;
        const VulkanSamplerDesc samplerDesc{
            .minFilter = VK_FILTER_NEAREST,
            .magFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        };
        sampler_ = VulkanSampler::Create(device, samplerDesc, error);
        if (!sampler_)
            return false;

        const VkDescriptorSetLayoutBinding textureBinding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        const VkDescriptorSetLayoutCreateInfo descriptorInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &textureBinding,
        };
        VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
        if (!Check(
                vkCreateDescriptorSetLayout(device_, &descriptorInfo, nullptr, &descriptorLayout),
                "vkCreateDescriptorSetLayout(texture)", error))
            return false;
        descriptorLayout_ = descriptorLayout;
        const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        const VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
        };
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (!Check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool),
                   "vkCreateDescriptorPool(texture)", error))
            return false;
        descriptorPool_ = pool;
        const VkDescriptorSetAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool_,
            .descriptorSetCount = 1,
            .pSetLayouts = &descriptorLayout_,
        };
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (!Check(vkAllocateDescriptorSets(device_, &allocateInfo, &set),
                   "vkAllocateDescriptorSets(texture)", error))
            return false;
        descriptorSet_ = set;
        const VkDescriptorImageInfo imageInfo{
            .sampler = sampler_->Get(),
            .imageView = view_->Get(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet_,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageInfo,
        };
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        const VkPipelineLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &descriptorLayout_,
        };
        VkPipelineLayout layout = VK_NULL_HANDLE;
        if (!Check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout),
                   "vkCreatePipelineLayout(texture)", error))
            return false;
        layout_ = layout;
        error.clear();
        return true;
    }

    VkResult VulkanTexturedQuadPipeline::WaitForUpload(std::string& error)
    {
        if (geometryUpload_)
        {
            const auto result = geometryUpload_->Wait(error);
            if (result == VK_NOT_READY && !geometryUpload_->IsPending())
                geometryUpload_.reset();
            else if (result != VK_SUCCESS)
                return result;
            else
            {
                auto geometry = geometryUpload_->TakeDestination(error);
                if (!geometry)
                    return VK_ERROR_INITIALIZATION_FAILED;
                geometry_ = std::move(*geometry);
                geometryUpload_.reset();
            }
        }
        if (imageUpload_)
        {
            const auto result = imageUpload_->Wait(error);
            if (result == VK_NOT_READY && !imageUpload_->IsPending())
                imageUpload_.reset();
            else if (result != VK_SUCCESS)
                return result;
            else
            {
                image_ = imageUpload_->TakeDestination(error);
                if (!image_)
                    return VK_ERROR_INITIALIZATION_FAILED;
                imageUpload_.reset();
            }
        }
        error.clear();
        return VK_SUCCESS;
    }

    VkResult VulkanTexturedQuadPipeline::DrainUploadForDestruction() noexcept
    {
        const auto geometryResult =
            geometryUpload_ ? geometryUpload_->DrainForDestruction() : VK_SUCCESS;
        const auto imageResult = imageUpload_ ? imageUpload_->DrainForDestruction() : VK_SUCCESS;
        return geometryResult == VK_SUCCESS ? imageResult : geometryResult;
    }

    void VulkanTexturedQuadPipeline::MarkUploadCompleteAfterQueueIdleForDestruction() noexcept
    {
        if (geometryUpload_)
            geometryUpload_->MarkCompleteAfterQueueIdleForDestruction();
        if (imageUpload_)
            imageUpload_->MarkCompleteAfterQueueIdleForDestruction();
    }

    void VulkanTexturedQuadPipeline::MarkUploadDeviceLostForDestruction() noexcept
    {
        if (geometryUpload_)
            geometryUpload_->MarkDeviceLostForDestruction();
        if (imageUpload_)
            imageUpload_->MarkDeviceLostForDestruction();
    }

    bool VulkanTexturedQuadPipeline::SetColorFormat(const VkFormat format, std::string& error)
    {
        error.clear();
        if (layout_ == VK_NULL_HANDLE || format == VK_FORMAT_UNDEFINED)
        {
            error = "Textured quad pipeline requires initialized resources and a color format";
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
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
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
                   "vkCreateGraphicsPipelines(texture)", error))
        {
            vkDestroyPipeline(device_, pipeline, nullptr);
            return false;
        }
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = pipeline;
        format_ = format;
        owl::foundation::LogMessage(owl::foundation::LogLevel::Info, "Vulkan",
                                    "Textured quad graphics pipeline created (colorFormat=" +
                                        std::to_string(static_cast<int>(format)) + ")");
        return true;
    }

    void VulkanTexturedQuadPipeline::RecordDraw(const VkCommandBuffer command,
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
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                                &descriptorSet_, 0, nullptr);
        const VkBuffer buffer = geometry_.Get();
        vkCmdBindVertexBuffers(command, 0, 1, &buffer, &vertexOffset);
        vkCmdBindIndexBuffer(command, buffer, IndexOffset, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(command, static_cast<std::uint32_t>(Indices.size()), 1, 0, 0, 0);
    }
} // namespace owl::vulkan
