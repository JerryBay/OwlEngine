#include "GpuTestContext.h"
#include "VulkanBuffer.h"
#include "VulkanTexturedQuadPipeline.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace owl::vulkan;
    constexpr VkExtent2D TargetExtent{64, 64};
    constexpr VkFormat TargetFormat = VK_FORMAT_R8G8B8A8_UNORM;
    using Pixel = std::array<unsigned int, 4>;

    std::optional<std::vector<std::byte>>
    DrawAndReadback(const owl::tests::GpuContext& context,
                    const VulkanTexturedQuadPipeline& pipeline,
                    const TextureDrawParameters& parameters, std::string& error);

    Pixel PixelAt(const std::vector<std::byte>& pixels, const std::size_t x, const std::size_t y)
    {
        const auto offset = (y * TargetExtent.width + x) * 4;
        return {std::to_integer<unsigned int>(pixels.at(offset)),
                std::to_integer<unsigned int>(pixels.at(offset + 1)),
                std::to_integer<unsigned int>(pixels.at(offset + 2)),
                std::to_integer<unsigned int>(pixels.at(offset + 3))};
    }

    bool NearPixel(const Pixel& actual, const Pixel& expected, const int tolerance)
    {
        for (std::size_t channel = 0; channel < actual.size(); ++channel)
        {
            if (std::abs(static_cast<int>(actual[channel]) - static_cast<int>(expected[channel])) >
                tolerance)
                return false;
        }
        return true;
    }

    struct DrawResources
    {
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkCommandPool pool = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        std::optional<VulkanBuffer> readback;
        bool pending = false;

        DrawResources() = default;
        DrawResources(const DrawResources&) = delete;
        DrawResources& operator=(const DrawResources&) = delete;

        ~DrawResources()
        {
            // Also protects pipeline/descriptor resources owned by the caller if a
            // failed assertion unwinds while this draw is still submitted.
            if (pending)
            {
                auto result = vkWaitForFences(device, 1, &fence, VK_TRUE,
                                              std::numeric_limits<std::uint64_t>::max());
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                    result = vkQueueWaitIdle(queue);
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                    std::terminate();
            }
            vkDestroyFence(device, fence, nullptr);
            vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyImageView(device, view, nullptr);
            if (image != VK_NULL_HANDLE)
                vmaDestroyImage(allocator, image, allocation);
        }
    };

    bool NativeSuccess(const VkResult result, const char* operation, std::string& error)
    {
        if (result == VK_SUCCESS)
            return true;
        error = std::string{operation} + " failed with VkResult " +
                std::to_string(static_cast<int>(result));
        return false;
    }

    std::optional<std::vector<std::byte>>
    DrawAndReadback(const owl::tests::GpuContext& context,
                    const VulkanTexturedQuadPipeline& pipeline,
                    const TextureDrawParameters& parameters, std::string& error)
    {
        DrawResources resources;
        resources.device = context.device->Get();
        resources.queue = context.device->GraphicsQueue();
        resources.allocator = context.allocator->Get();
        const VkImageCreateInfo imageInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = TargetFormat,
            .extent = {TargetExtent.width, TargetExtent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        const VmaAllocationCreateInfo allocationInfo{
            .usage = VMA_MEMORY_USAGE_AUTO,
            .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        };
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        if (!NativeSuccess(vmaCreateImage(resources.allocator, &imageInfo, &allocationInfo, &image,
                                          &allocation, nullptr),
                           "vmaCreateImage(texture sampling target)", error))
            return std::nullopt;
        resources.image = image;
        resources.allocation = allocation;
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = TargetFormat,
            .subresourceRange = range,
        };
        VkImageView view = VK_NULL_HANDLE;
        if (!NativeSuccess(vkCreateImageView(resources.device, &viewInfo, nullptr, &view),
                           "vkCreateImageView(texture sampling target)", error))
            return std::nullopt;
        resources.view = view;
        constexpr std::size_t byteCount = TargetExtent.width * TargetExtent.height * 4;
        const VulkanBufferDesc readbackDesc{
            .size = byteCount,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            .preferredMemory = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            .hostAccess = BufferHostAccess::Random,
        };
        resources.readback = VulkanBuffer::Create(*context.allocator, readbackDesc, error);
        if (!resources.readback)
            return std::nullopt;
        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = *context.device->QueueFamilies().graphicsFamily,
        };
        VkCommandPool pool = VK_NULL_HANDLE;
        if (!NativeSuccess(vkCreateCommandPool(resources.device, &poolInfo, nullptr, &pool),
                           "vkCreateCommandPool(texture sampling)", error))
            return std::nullopt;
        resources.pool = pool;
        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer command = VK_NULL_HANDLE;
        if (!NativeSuccess(vkAllocateCommandBuffers(resources.device, &allocateInfo, &command),
                           "vkAllocateCommandBuffers(texture sampling)", error))
            return std::nullopt;
        const VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        if (!NativeSuccess(vkBeginCommandBuffer(command, &beginInfo),
                           "vkBeginCommandBuffer(texture sampling)", error))
            return std::nullopt;
        VkImageMemoryBarrier2 imageBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };
        const VkDependencyInfo imageDependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &imageBarrier,
        };
        vkCmdPipelineBarrier2(command, &imageDependency);
        const VkRenderingAttachmentInfo attachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {{0.0F, 0.0F, 0.0F, 1.0F}}},
        };
        const VkRenderingInfo rendering{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {{0, 0}, TargetExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment,
        };
        vkCmdBeginRendering(command, &rendering);
        pipeline.RecordDraw(command, TargetExtent, parameters);
        vkCmdEndRendering(command);
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier2(command, &imageDependency);
        const VkBufferImageCopy copy{
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageExtent = {TargetExtent.width, TargetExtent.height, 1},
        };
        vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               resources.readback->Get(), 1, &copy);
        const VkBufferMemoryBarrier2 hostBarrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
            .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = resources.readback->Get(),
            .offset = 0,
            .size = byteCount,
        };
        const VkDependencyInfo hostDependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &hostBarrier,
        };
        vkCmdPipelineBarrier2(command, &hostDependency);
        if (!NativeSuccess(vkEndCommandBuffer(command), "vkEndCommandBuffer(texture sampling)",
                           error))
            return std::nullopt;
        const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        if (!NativeSuccess(vkCreateFence(resources.device, &fenceInfo, nullptr, &fence),
                           "vkCreateFence(texture sampling)", error))
            return std::nullopt;
        resources.fence = fence;
        const VkCommandBufferSubmitInfo commandInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = command,
        };
        const VkSubmitInfo2 submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandInfo,
        };
        const auto submitted = vkQueueSubmit2(resources.queue, 1, &submitInfo, fence);
        resources.pending = submitted == VK_SUCCESS;
        if (!NativeSuccess(submitted, "vkQueueSubmit2(texture sampling)", error))
            return std::nullopt;
        const auto waited = vkWaitForFences(resources.device, 1, &fence, VK_TRUE,
                                            std::numeric_limits<std::uint64_t>::max());
        if (waited == VK_SUCCESS || waited == VK_ERROR_DEVICE_LOST)
            resources.pending = false;
        if (!NativeSuccess(waited, "vkWaitForFences(texture sampling)", error))
            return std::nullopt;
        std::vector<std::byte> output(byteCount);
        if (!resources.readback->Read(0, output, error))
            return std::nullopt;
        error.clear();
        return output;
    }

    class PipelineDrainGuard
    {
    public:
        PipelineDrainGuard(VulkanTexturedQuadPipeline& pipeline, const VkDevice device) noexcept
            : pipeline_(pipeline), device_(device)
        {
        }
        PipelineDrainGuard(const PipelineDrainGuard&) = delete;
        PipelineDrainGuard& operator=(const PipelineDrainGuard&) = delete;
        ~PipelineDrainGuard()
        {
            auto result = pipeline_.DrainUploadForDestruction();
            if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                result = vkDeviceWaitIdle(device_);
            if (result == VK_SUCCESS)
                pipeline_.MarkUploadCompleteAfterQueueIdleForDestruction();
            else if (result == VK_ERROR_DEVICE_LOST)
                pipeline_.MarkUploadDeviceLostForDestruction();
            else
                std::terminate();
        }

    private:
        VulkanTexturedQuadPipeline& pipeline_;
        VkDevice device_;
    };

    void VerifySampledMips(const TextureMipMode mode)
    {
        if (!owl::tests::IsGpuTestEnabled())
            SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run texture sampling readback");
        std::string error;
        auto context = owl::tests::CreateGpuContext(error);
        INFO(error);
        REQUIRE(context);
        VulkanTexturedQuadPipeline pipeline;
        PipelineDrainGuard drain{pipeline, context->device->Get()};
        const std::filesystem::path assets{OWL_TEXTURE_TEST_ASSET_DIR};
        REQUIRE(pipeline.Initialize(
            *context->device, *context->allocator,
            {assets / "texture_mips.vert.spv", assets / "texture_mips.frag.spv"}, error, mode));
        REQUIRE(pipeline.MipLevels() == 7);
        if (mode == TextureMipMode::ForceCpu)
            CHECK_FALSE(pipeline.UsesGpuMipGeneration());
        CAPTURE(pipeline.UsesGpuMipGeneration());
        REQUIRE(pipeline.SetColorFormat(TargetFormat, error));

        const auto level0 = DrawAndReadback(*context, pipeline, {.lod = 0.0F}, error);
        INFO(error);
        REQUIRE(level0);
        // Geometry covers pixel coordinates [7.04,56.96]. These centers map safely
        // inside the original marker, dark square and light square, away from boundaries.
        CHECK(PixelAt(*level0, 9, 9) == Pixel{232, 53, 53, 255});
        CHECK(PixelAt(*level0, 16, 10) == Pixel{34, 41, 50, 255});
        CHECK(PixelAt(*level0, 23, 10) == Pixel{244, 247, 249, 255});
        CHECK(PixelAt(*level0, 0, 0) == Pixel{0, 0, 0, 255});

        const auto level6 = DrawAndReadback(*context, pipeline, {.lod = 6.0F}, error);
        REQUIRE(level6);
        // Independent analytical oracle: 31 light blocks, 32 dark blocks and 1 marker.
        // GPU blits and per-level UNORM quantization can differ by a few byte values.
        constexpr Pixel fullAverage{139, 141, 146, 255};
        for (const auto [x, y] :
             {std::pair{9U, 9U}, std::pair{16U, 10U}, std::pair{23U, 10U}, std::pair{48U, 48U}})
        {
            CAPTURE(x, y, PixelAt(*level6, x, y));
            CHECK(NearPixel(PixelAt(*level6, x, y), fullAverage, 2));
        }

        // Four framebuffer pixels span all 64 texels: dUV/dPixel * 64 = 16,
        // so implicit minification chooses log2(16) = LOD 4. The quad still covers
        // a 4x4 block of pixels, allowing checks clear of its rasterization edges.
        constexpr float minifiedScale = 4.0F / (0.78F * static_cast<float>(TargetExtent.width));
        const auto implicit = DrawAndReadback(*context, pipeline, {.scale = minifiedScale}, error);
        REQUIRE(implicit);
        const auto explicit4 =
            DrawAndReadback(*context, pipeline, {.scale = minifiedScale, .lod = 4.0F}, error);
        REQUIRE(explicit4);
        const auto baseOnly =
            DrawAndReadback(*context, pipeline, {.scale = minifiedScale, .lod = 0.0F}, error);
        REQUIRE(baseOnly);
        bool differsFromBase = false;
        for (std::size_t y = 30; y < 34; ++y)
        {
            for (std::size_t x = 30; x < 34; ++x)
            {
                const auto actual = PixelAt(*implicit, x, y);
                CAPTURE(x, y, actual);
                CHECK(actual[3] == 255);
                CHECK(actual[0] >= 130);
                CHECK(actual[0] <= 145);
                CHECK(NearPixel(actual, PixelAt(*explicit4, x, y), 2));
                differsFromBase =
                    differsFromBase || !NearPixel(actual, PixelAt(*baseOnly, x, y), 16);
            }
        }
        CHECK(differsFromBase);
        CHECK(PixelAt(*implicit, 29, 29) == Pixel{0, 0, 0, 255});
    }
} // namespace

TEST_CASE("Automatically generated mip texture samples explicit and implicit levels",
          "[vulkan][texture][mip][sampling][integration]")
{
    VerifySampledMips(owl::vulkan::TextureMipMode::Generate);
}

TEST_CASE("CPU-generated mip texture samples explicit and implicit levels",
          "[vulkan][texture][mip][sampling][integration]")
{
    VerifySampledMips(owl::vulkan::TextureMipMode::ForceCpu);
}
