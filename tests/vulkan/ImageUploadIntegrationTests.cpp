#include "GpuTestContext.h"
#include "VulkanBuffer.h"
#include "VulkanImageUpload.h"
#include "VulkanMipChain.h"

#include <catch2/catch_test_macros.hpp>
#include <owl/foundation/Log.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace owl::vulkan;

    // REQUIRE may unwind at any point. A pending owner must observe completion before release.
    class UploadDrainGuard
    {
    public:
        explicit UploadDrainGuard(VulkanImageUpload& upload) noexcept : upload_(upload) {}
        UploadDrainGuard(const UploadDrainGuard&) = delete;
        UploadDrainGuard& operator=(const UploadDrainGuard&) = delete;

        ~UploadDrainGuard()
        {
            if (upload_.IsPending())
                static_cast<void>(upload_.DrainForDestruction());
            if (upload_.IsPending())
                std::terminate();
        }

    private:
        VulkanImageUpload& upload_;
    };

    struct ReadbackResources
    {
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VkCommandPool pool = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        std::optional<VulkanBuffer> buffer;
        bool pending = false;

        ReadbackResources() = default;
        ReadbackResources(const ReadbackResources&) = delete;
        ReadbackResources& operator=(const ReadbackResources&) = delete;

        ~ReadbackResources()
        {
            if (pending)
            {
                VkResult result = vkWaitForFences(
                    device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                    result = vkQueueWaitIdle(queue);
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                    std::terminate();
            }
            if (fence != VK_NULL_HANDLE)
                vkDestroyFence(device, fence, nullptr);
            if (pool != VK_NULL_HANDLE)
                vkDestroyCommandPool(device, pool, nullptr);
        }
    };

    VmaStatistics AllocationStatistics(const VulkanAllocator& allocator)
    {
        VmaTotalStatistics statistics{};
        vmaCalculateStatistics(allocator.Get(), &statistics);
        return statistics.total.statistics;
    }

    VulkanImageDesc UploadDesc(const VkExtent2D extent, const VkFormat format)
    {
        return {
            .extent = extent,
            .format = format,
            .mipLevels = 1,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        };
    }

    enum class PixelPattern
    {
        Checkerboard,
        Asymmetric,
    };

    std::vector<std::byte> Pixels(const VkExtent2D extent, const PixelPattern pattern)
    {
        std::vector<std::byte> result(static_cast<std::size_t>(extent.width) * extent.height * 4);
        for (std::uint32_t y = 0; y < extent.height; ++y)
        {
            for (std::uint32_t x = 0; x < extent.width; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * extent.width + x) * 4;
                if (pattern == PixelPattern::Checkerboard)
                {
                    const bool light = ((x + y) & 1U) != 0;
                    result[offset] = static_cast<std::byte>(light ? 224 : 32);
                    result[offset + 1] = static_cast<std::byte>(light ? 128 : 48);
                    result[offset + 2] = static_cast<std::byte>(light ? 64 : 192);
                    result[offset + 3] = std::byte{255};
                }
                else
                {
                    // Unequal channels and asymmetric spatial terms expose row/channel swaps.
                    result[offset] = static_cast<std::byte>((x * 17U + y * 53U + 11U) & 255U);
                    result[offset + 1] = static_cast<std::byte>((x * 71U + y * 7U + 128U) & 255U);
                    result[offset + 2] = static_cast<std::byte>((x * 3U + y * 29U + 224U) & 255U);
                    result[offset + 3] = static_cast<std::byte>((x * 41U + y * 13U + 63U) & 255U);
                }
            }
        }
        return result;
    }

    bool NativeSuccess(const VkResult result, const char* operation, std::string& error)
    {
        if (result == VK_SUCCESS)
            return true;
        error = std::string{operation} + " failed with VkResult " +
                std::to_string(static_cast<int>(result));
        return false;
    }

    // Independent of the upload recorder: tightly packed buffer layout, mip 0, color aspect.
    // Restores the upload's promised layout so another readback can use the same entry contract.
    bool ReadbackImage(const VulkanDevice& device, const VulkanAllocator& allocator,
                       const VulkanImage& image, std::span<std::byte> output,
                       VkMemoryPropertyFlags& memoryProperties, std::string& error,
                       const std::uint32_t mipLevel = 0)
    {
        ReadbackResources resources;
        resources.device = device.Get();
        resources.queue = device.GraphicsQueue();
        const VulkanBufferDesc bufferDesc{
            .size = output.size(),
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            .preferredMemory = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            .hostAccess = BufferHostAccess::Random,
        };
        resources.buffer = VulkanBuffer::Create(allocator, bufferDesc, error);
        if (!resources.buffer)
            return false;
        memoryProperties = resources.buffer->MemoryProperties();
        const auto family = device.QueueFamilies().graphicsFamily;
        if (!family)
        {
            error = "Image readback requires a graphics queue family";
            return false;
        }
        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = *family,
        };
        VkCommandPool pool = VK_NULL_HANDLE;
        if (!NativeSuccess(vkCreateCommandPool(device.Get(), &poolInfo, nullptr, &pool),
                           "vkCreateCommandPool(image readback)", error))
            return false;
        resources.pool = pool;
        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = resources.pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer command = VK_NULL_HANDLE;
        if (!NativeSuccess(vkAllocateCommandBuffers(device.Get(), &allocateInfo, &command),
                           "vkAllocateCommandBuffers(image readback)", error))
            return false;
        const VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        if (!NativeSuccess(vkBeginCommandBuffer(command, &beginInfo),
                           "vkBeginCommandBuffer(image readback)", error))
            return false;
        const VkImageSubresourceRange range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = mipLevel,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        // READ_ONLY layout does not mean a shader was the last producer. Include upload writes
        // and the previous layout transition, as well as any allowed fragment sampled reads.
        const VkImageMemoryBarrier2 toCopy{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image.Get(),
            .subresourceRange = range,
        };
        const VkDependencyInfo beforeCopy{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toCopy,
        };
        vkCmdPipelineBarrier2(command, &beforeCopy);
        const VkBufferImageCopy copy{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {std::max(1U, image.Desc().extent.width >> mipLevel),
                            std::max(1U, image.Desc().extent.height >> mipLevel), 1},
        };
        vkCmdCopyImageToBuffer(command, image.Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               resources.buffer->Get(), 1, &copy);
        const VkBufferMemoryBarrier2 toHost{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
            .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = resources.buffer->Get(),
            .offset = 0,
            .size = resources.buffer->Size(),
        };
        const VkImageMemoryBarrier2 toSampling{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image.Get(),
            .subresourceRange = range,
        };
        const VkDependencyInfo afterCopy{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &toHost,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toSampling,
        };
        vkCmdPipelineBarrier2(command, &afterCopy);
        if (!NativeSuccess(vkEndCommandBuffer(command), "vkEndCommandBuffer(image readback)", error))
            return false;
        const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        if (!NativeSuccess(vkCreateFence(device.Get(), &fenceInfo, nullptr, &fence),
                           "vkCreateFence(image readback)", error))
            return false;
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
        const VkResult submitted = vkQueueSubmit2(resources.queue, 1, &submitInfo, resources.fence);
        resources.pending = submitted == VK_SUCCESS;
        if (!NativeSuccess(submitted, "vkQueueSubmit2(image readback)", error))
            return false;
        const VkResult waited = vkWaitForFences(
            resources.device, 1, &resources.fence, VK_TRUE,
            std::numeric_limits<std::uint64_t>::max());
        if (waited == VK_SUCCESS || waited == VK_ERROR_DEVICE_LOST)
            resources.pending = false;
        if (!NativeSuccess(waited, "vkWaitForFences(image readback)", error))
            return false;
        return resources.buffer->Read(0, output, error);
    }
} // namespace

TEST_CASE("Vulkan image upload roundtrips RGBA8 pixels and restores sampled layout",
          "[vulkan][image-upload][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the image upload test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto baseline = AllocationStatistics(*context->allocator);
    constexpr std::array<VkExtent2D, 5> extents{{{1, 1}, {1, 7}, {7, 1}, {7, 3}, {17, 9}}};
    VkMemoryPropertyFlags stagingProperties = 0;
    VkMemoryPropertyFlags readbackProperties = 0;
    for (const auto format : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB})
    {
        for (const auto extent : extents)
        {
            for (const auto pattern : {PixelPattern::Checkerboard, PixelPattern::Asymmetric})
            {
                CAPTURE(format, extent.width, extent.height, static_cast<int>(pattern));
                {
                    const auto expected = Pixels(extent, pattern);
                    auto input = expected;
                    auto upload = VulkanImageUpload::Create(
                        *context->device, *context->allocator, UploadDesc(extent, format), input, error);
                    INFO(error);
                    REQUIRE(upload);
                    UploadDrainGuard guard{*upload};
                    stagingProperties = upload->StagingMemoryProperties();
                    CHECK((stagingProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);
                    CHECK(AllocationStatistics(*context->allocator).allocationCount ==
                          baseline.allocationCount + 2);
                    // Create owns its staging bytes; later changes to caller storage cannot leak in.
                    std::fill(input.begin(), input.end(), std::byte{});
                    REQUIRE(upload->Submit(error) == VK_SUCCESS);
                    REQUIRE(upload->Wait(error) == VK_SUCCESS);
                    CHECK_FALSE(upload->IsPending());
                    CHECK(AllocationStatistics(*context->allocator).allocationCount ==
                          baseline.allocationCount + 1);
                    auto image = upload->TakeDestination(error);
                    REQUIRE(image);
                    CHECK(image->Desc().format == format);
                    std::vector<std::byte> actual(expected.size());
                    for (int repeat = 0; repeat < 2; ++repeat)
                    {
                        CAPTURE(repeat);
                        std::fill(actual.begin(), actual.end(), std::byte{});
                        REQUIRE(ReadbackImage(*context->device, *context->allocator, *image,
                                              actual, readbackProperties, error));
                        CHECK(actual == expected);
                        CHECK(AllocationStatistics(*context->allocator).allocationCount ==
                              baseline.allocationCount + 1);
                    }
                }
                const auto released = AllocationStatistics(*context->allocator);
                CHECK(released.allocationCount == baseline.allocationCount);
                CHECK(released.allocationBytes == baseline.allocationBytes);
            }
        }
    }
    owl::foundation::LogMessage(owl::foundation::LogLevel::Info, "Vulkan",
        "Image upload byte roundtrip verified: RGBA8 UNORM/SRGB, 5 extents, 2 patterns, "
        "2 readbacks each; staging flags=" + std::to_string(stagingProperties) +
        ", readback flags=" + std::to_string(readbackProperties));
}

TEST_CASE("Vulkan image upload preserves pending ownership through moves and replacement",
          "[vulkan][image-upload][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the image upload move test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto baseline = AllocationStatistics(*context->allocator);
    // Parent owners precede every child, including when a REQUIRE unwinds after their moves.
    std::optional<VulkanDevice> movedDevice;
    std::optional<VulkanAllocator> movedAllocator;
    {
        const auto desc = UploadDesc({7, 3}, VK_FORMAT_R8G8B8A8_UNORM);
        const auto pixels = Pixels(desc.extent, PixelPattern::Asymmetric);
        auto source = VulkanImageUpload::Create(
            *context->device, *context->allocator, desc, pixels, error);
        REQUIRE(source);
        UploadDrainGuard sourceGuard{*source};
        auto replacement = VulkanImageUpload::Create(
            *context->device, *context->allocator, desc, pixels, error);
        REQUIRE(replacement);
        UploadDrainGuard replacementGuard{*replacement};
        CHECK(AllocationStatistics(*context->allocator).allocationCount ==
              baseline.allocationCount + 4);
        CHECK_FALSE(source->TakeDestination(error));
        CHECK_FALSE(error.empty());
        CHECK(source->Wait(error, 0) != VK_SUCCESS);
        CHECK_FALSE(source->IsPending());

        movedDevice.emplace(std::move(*context->device));
        movedAllocator.emplace(std::move(*context->allocator));
        CHECK_FALSE(context->device->IsValid());
        CHECK_FALSE(context->allocator->IsValid());
        REQUIRE(source->Submit(error) == VK_SUCCESS);
        CHECK(source->IsPending());
        CHECK_FALSE(source->TakeDestination(error));
        CHECK(source->Submit(error) != VK_SUCCESS);
        CHECK(source->IsPending());

        VulkanImageUpload moved{std::move(*source)};
        UploadDrainGuard movedGuard{moved};
        CHECK_FALSE(source->IsPending());
        CHECK_FALSE(source->TakeDestination(error));
        CHECK(moved.IsPending());
        // Replacing an unsubmitted operation releases its own image/staging pair safely.
        *replacement = std::move(moved);
        CHECK_FALSE(moved.IsPending());
        CHECK(replacement->IsPending());
        CHECK(AllocationStatistics(*movedAllocator).allocationCount == baseline.allocationCount + 2);
        const VkResult polled = replacement->Wait(error, 0);
        REQUIRE((polled == VK_SUCCESS || polled == VK_TIMEOUT));
        if (polled == VK_TIMEOUT)
        {
            CHECK(replacement->IsPending());
            CHECK_FALSE(replacement->TakeDestination(error));
        }
        REQUIRE(replacement->Wait(error) == VK_SUCCESS);
        CHECK_FALSE(replacement->IsPending());
        CHECK(AllocationStatistics(*movedAllocator).allocationCount == baseline.allocationCount + 1);
        CHECK(replacement->Submit(error) != VK_SUCCESS);
        CHECK_FALSE(replacement->IsPending());
        auto image = replacement->TakeDestination(error);
        REQUIRE(image);
        CHECK_FALSE(replacement->TakeDestination(error));
        CHECK_FALSE(error.empty());
        std::vector<std::byte> actual(pixels.size());
        VkMemoryPropertyFlags properties = 0;
        REQUIRE(ReadbackImage(*movedDevice, *movedAllocator, *image, actual, properties, error));
        CHECK(actual == pixels);
    }
    const auto released = AllocationStatistics(*movedAllocator);
    CHECK(released.allocationCount == baseline.allocationCount);
    CHECK(released.allocationBytes == baseline.allocationBytes);
    *context->device = std::move(*movedDevice);
    *context->allocator = std::move(*movedAllocator);
}

TEST_CASE("Vulkan image upload drains completion before transferring the destination",
          "[vulkan][image-upload][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the image upload drain test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto baseline = AllocationStatistics(*context->allocator);
    {
        const auto desc = UploadDesc({1, 1}, VK_FORMAT_R8G8B8A8_SRGB);
        const auto pixels = Pixels(desc.extent, PixelPattern::Asymmetric);
        auto upload = VulkanImageUpload::Create(
            *context->device, *context->allocator, desc, pixels, error);
        REQUIRE(upload);
        UploadDrainGuard guard{*upload};
        REQUIRE(upload->Submit(error) == VK_SUCCESS);
        CHECK(upload->IsPending());
        REQUIRE(upload->DrainForDestruction() == VK_SUCCESS);
        CHECK_FALSE(upload->IsPending());
        CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount + 1);
        auto image = upload->TakeDestination(error);
        REQUIRE(image);
        std::vector<std::byte> actual(pixels.size());
        VkMemoryPropertyFlags properties = 0;
        REQUIRE(ReadbackImage(*context->device, *context->allocator, *image, actual, properties, error));
        CHECK(actual == pixels);
    }
    CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
    CHECK(AllocationStatistics(*context->allocator).allocationBytes == baseline.allocationBytes);
}

TEST_CASE("Vulkan image upload accepts externally proven queue completion",
          "[vulkan][image-upload][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the image upload queue-idle test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto baseline = AllocationStatistics(*context->allocator);
    {
        const auto desc = UploadDesc({7, 3}, VK_FORMAT_R8G8B8A8_UNORM);
        const auto pixels = Pixels(desc.extent, PixelPattern::Asymmetric);
        auto upload = VulkanImageUpload::Create(
            *context->device, *context->allocator, desc, pixels, error);
        REQUIRE(upload);
        UploadDrainGuard guard{*upload};
        REQUIRE(upload->Submit(error) == VK_SUCCESS);
        REQUIRE(upload->IsPending());
        REQUIRE(vkQueueWaitIdle(context->device->GraphicsQueue()) == VK_SUCCESS);
        upload->MarkCompleteAfterQueueIdleForDestruction();
        CHECK_FALSE(upload->IsPending());
        auto image = upload->TakeDestination(error);
        REQUIRE(image);
        std::vector<std::byte> actual(pixels.size());
        VkMemoryPropertyFlags properties = 0;
        REQUIRE(ReadbackImage(*context->device, *context->allocator, *image,
                              actual, properties, error));
        CHECK(actual == pixels);
    }
    CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
    CHECK(AllocationStatistics(*context->allocator).allocationBytes == baseline.allocationBytes);
}

TEST_CASE("Vulkan image upload rejects invalid requests without retaining allocations",
          "[vulkan][image-upload][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the image upload rejection test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto baseline = AllocationStatistics(*context->allocator);
    const auto desc = UploadDesc({7, 3}, VK_FORMAT_R8G8B8A8_UNORM);
    const auto pixels = Pixels(desc.extent, PixelPattern::Asymmetric);
    auto rejected = [&](const VulkanImageDesc& request, std::span<const std::byte> bytes)
    {
        CHECK_FALSE(VulkanImageUpload::Create(
            *context->device, *context->allocator, request, bytes, error));
        CHECK_FALSE(error.empty());
        CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
        CHECK(AllocationStatistics(*context->allocator).allocationBytes == baseline.allocationBytes);
    };
    rejected(desc, {});
    rejected(desc, std::span<const std::byte>{pixels}.first(pixels.size() - 1));
    auto oversized = pixels;
    oversized.push_back(std::byte{});
    rejected(desc, oversized);
    auto invalid = desc;
    invalid.extent.width = 0;
    rejected(invalid, pixels);
    invalid = desc;
    invalid.mipLevels = 4;
    rejected(invalid, pixels);
    invalid = desc;
    invalid.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    rejected(invalid, pixels);
    invalid = desc;
    invalid.format = VK_FORMAT_B8G8R8A8_UNORM;
    rejected(invalid, pixels);
    invalid = desc;
    invalid.extent = {UINT32_MAX, UINT32_MAX};
    rejected(invalid, pixels);

    auto otherDevice = VulkanDevice::Create(*context->selection, error);
    REQUIRE(otherDevice);
    CHECK_FALSE(VulkanImageUpload::Create(*otherDevice, *context->allocator, desc, pixels, error));
    CHECK_FALSE(error.empty());
    CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
    CHECK(AllocationStatistics(*context->allocator).allocationBytes == baseline.allocationBytes);
    {
        auto sampledOnly = desc;
        sampledOnly.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        auto unsubmitted = VulkanImageUpload::Create(
            *context->device, *context->allocator, sampledOnly, pixels, error);
        REQUIRE(unsubmitted);
        CHECK_FALSE(unsubmitted->IsPending());
        CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount + 2);
    }
    CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
    CHECK(AllocationStatistics(*context->allocator).allocationBytes == baseline.allocationBytes);
}

TEST_CASE("CPU mip fallback uploads every level of an odd-sized image",
          "[vulkan][mip][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the mip upload test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto baseline = AllocationStatistics(*context->allocator);
    {
        auto desc = UploadDesc({7, 3}, VK_FORMAT_R8G8B8A8_SRGB);
        desc.mipLevels = 3;
        const auto pixels = Pixels(desc.extent, PixelPattern::Asymmetric);
        const auto expected = detail::BuildCpuMipChain(desc, pixels, error);
        REQUIRE(expected);
        auto upload = VulkanImageUpload::Create(
            *context->device, *context->allocator, desc, pixels, error,
            MipGenerationMode::ForceCpu);
        INFO(error);
        REQUIRE(upload);
        UploadDrainGuard guard{*upload};
        REQUIRE(upload->Path() == MipUploadPath::CpuUpload);
        REQUIRE(upload->Submit(error) == VK_SUCCESS);
        REQUIRE(upload->Wait(error) == VK_SUCCESS);
        auto image = upload->TakeDestination(error);
        REQUIRE(image);
        for (std::uint32_t level = 0; level < desc.mipLevels; ++level)
        {
            const auto& entry = expected->levels[level];
            CAPTURE(level);
            const auto size = static_cast<std::size_t>(entry.extent.width) * entry.extent.height * 4;
            std::vector<std::byte> actual(size);
            VkMemoryPropertyFlags properties = 0;
            REQUIRE(ReadbackImage(*context->device, *context->allocator, *image,
                                  actual, properties, error, level));
            CHECK(std::equal(actual.begin(), actual.end(),
                             expected->bytes.begin() + entry.offset));
        }
    }
    CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
}

TEST_CASE("Mip upload automatically falls back when transfer-source usage is absent",
          "[vulkan][mip][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the mip fallback test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    auto desc = UploadDesc({2, 2}, VK_FORMAT_R8G8B8A8_UNORM);
    desc.mipLevels = 2;
    desc.usage &= ~VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    constexpr std::array<std::byte, 16> pixels{
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    auto upload = VulkanImageUpload::Create(*context->device, *context->allocator,
                                            desc, pixels, error);
    INFO(error);
    REQUIRE(upload);
    UploadDrainGuard guard{*upload};
    CHECK(upload->Path() == MipUploadPath::CpuUpload);
    REQUIRE(upload->Submit(error) == VK_SUCCESS);
    REQUIRE(upload->Wait(error) == VK_SUCCESS);
    auto image = upload->TakeDestination(error);
    REQUIRE(image);
    CHECK(image->Desc().mipLevels == 2);
}

TEST_CASE("GPU blit creates readable lower mips when the format supports linear blits",
          "[vulkan][mip][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the mip blit test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context->device->PhysicalDevice(),
                                        VK_FORMAT_R8G8B8A8_UNORM, &properties);
    constexpr auto required = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        SKIP("This device does not support linear RGBA8 blits");
    const auto baseline = AllocationStatistics(*context->allocator);
    {
        auto desc = UploadDesc({4, 4}, VK_FORMAT_R8G8B8A8_UNORM);
        desc.mipLevels = 3;
        std::array<std::byte, 4 * 4 * 4> pixels{};
        for (std::size_t y = 0; y < 4; ++y)
        {
            for (std::size_t x = 0; x < 4; ++x)
            {
                const auto offset = (y * 4 + x) * 4;
                pixels[offset] = static_cast<std::byte>(x < 2 ? 0 : 255);
                pixels[offset + 1] = std::byte{64};
                pixels[offset + 2] = static_cast<std::byte>(y < 2 ? 0 : 255);
                pixels[offset + 3] = std::byte{255};
            }
        }
        auto upload = VulkanImageUpload::Create(
            *context->device, *context->allocator, desc, pixels, error);
        INFO(error);
        REQUIRE(upload);
        UploadDrainGuard guard{*upload};
        REQUIRE(upload->Path() == MipUploadPath::GpuBlit);
        REQUIRE(upload->Submit(error) == VK_SUCCESS);
        REQUIRE(upload->Wait(error) == VK_SUCCESS);
        auto image = upload->TakeDestination(error);
        REQUIRE(image);
        std::array<std::byte, 16> level1{};
        VkMemoryPropertyFlags memory = 0;
        REQUIRE(ReadbackImage(*context->device, *context->allocator, *image,
                              level1, memory, error, 1));
        CHECK(level1[0] == std::byte{0});
        CHECK(level1[4] == std::byte{255});
        CHECK(level1[8 + 2] == std::byte{255});
        std::array<std::byte, 4> level2{};
        REQUIRE(ReadbackImage(*context->device, *context->allocator, *image,
                              level2, memory, error, 2));
        const auto red = std::to_integer<unsigned int>(level2[0]);
        const auto blue = std::to_integer<unsigned int>(level2[2]);
        CHECK((red >= 127 && red <= 128));
        CHECK(level2[1] == std::byte{64});
        CHECK((blue >= 127 && blue <= 128));
        CHECK(level2[3] == std::byte{255});
    }
    CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
}
