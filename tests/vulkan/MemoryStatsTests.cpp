#include "GpuTestContext.h"
#include "VulkanBuffer.h"
#include "VulkanImage.h"

#include <catch2/catch_test_macros.hpp>
#include <owl/vulkan/VulkanTriangle.h>

#include <utility>

namespace
{
    using namespace owl::vulkan;

    VulkanMemoryStats CheckSnapshot(const VulkanAllocator& allocator)
    {
        const auto snapshot = allocator.QueryMemoryStats();
        REQUIRE(snapshot);
        // Independent detailed traversal verifies the basic totals, including every heap.
        VmaTotalStatistics native{};
        vmaCalculateStatistics(allocator.Get(), &native);
        CHECK(snapshot->allocationCount == native.total.statistics.allocationCount);
        CHECK(snapshot->allocationBytes == native.total.statistics.allocationBytes);
        CHECK(snapshot->blockCount == native.total.statistics.blockCount);
        CHECK(snapshot->blockBytes == native.total.statistics.blockBytes);
        CHECK(snapshot->blockBytes >= snapshot->allocationBytes);
        return *snapshot;
    }
} // namespace

TEST_CASE("Empty memory owners report unavailable statistics", "[vulkan][memory-stats]")
{
    VulkanAllocator allocator;
    CHECK_FALSE(allocator.QueryMemoryStats());
    VulkanAllocator moved{std::move(allocator)};
    CHECK_FALSE(allocator.QueryMemoryStats());
    CHECK_FALSE(moved.QueryMemoryStats());
    VulkanTriangle renderer;
    CHECK_FALSE(renderer.QueryMemoryStats());
    VulkanTriangle movedRenderer{std::move(renderer)};
    CHECK_FALSE(renderer.QueryMemoryStats());
    CHECK_FALSE(movedRenderer.QueryMemoryStats());
    CHECK_FALSE(renderer.QueryResourceStats());
    CHECK_FALSE(movedRenderer.QueryResourceStats());
    CHECK(VulkanBuffer{}.ResourceStats().buffers.count == 0);
    CHECK(VulkanImage{}.ResourceStats().images.count == 0);
}

TEST_CASE("Allocator snapshots track live buffer and image storage through moves and release",
          "[vulkan][memory-stats][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run allocator statistics checks");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto baseline = CheckSnapshot(*context->allocator);
    CHECK(baseline.allocationCount == 0);
    CHECK(baseline.allocationBytes == 0);

    // Declare the moved parent before its children so assertion unwinding is safe too.
    VulkanAllocator movedAllocator;
    {
        const VulkanBufferDesc bufferDesc{
            .size = 67,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            .hostAccess = BufferHostAccess::SequentialWrite,
        };
        auto buffer = VulkanBuffer::Create(*context->allocator, bufferDesc, error);
        INFO(error);
        REQUIRE(buffer);
        const auto withBuffer = CheckSnapshot(*context->allocator);
        CHECK(withBuffer.allocationCount == baseline.allocationCount + 1);
        CHECK(withBuffer.allocationBytes == baseline.allocationBytes + buffer->AllocationSize());
        CHECK(withBuffer.blockCount > 0);
        const auto bufferUsage = buffer->ResourceStats();
        CHECK(bufferUsage.buffers.count == 1);
        CHECK(bufferUsage.buffers.requestedBytes == 67);
        CHECK(bufferUsage.buffers.allocationBytes == buffer->AllocationSize());
        VulkanBuffer movedBuffer{std::move(*buffer)};
        CHECK(buffer->ResourceStats().buffers.count == 0);
        CHECK(movedBuffer.ResourceStats().buffers.requestedBytes == 67);

        const VulkanImageDesc imageDesc{
            .extent = {7, 3},
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .mipLevels = 3,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        };
        auto image = VulkanImage::Create(*context->allocator, imageDesc, error);
        INFO(error);
        REQUIRE(image);
        const auto withImage = CheckSnapshot(*context->allocator);
        CHECK(withImage.allocationCount == withBuffer.allocationCount + 1);
        CHECK(withImage.allocationBytes > withBuffer.allocationBytes);
        const auto imageUsage = image->ResourceStats();
        CHECK(imageUsage.images.count == 1);
        // 7x3 + 3x1 + 1x1 RGBA8 texels, independent of optimal native image padding.
        CHECK(imageUsage.images.requestedBytes == 100);
        CHECK(imageUsage.images.allocationBytes ==
              withImage.allocationBytes - withBuffer.allocationBytes);
        VulkanImage movedImage{std::move(*image)};
        CHECK(image->ResourceStats().images.count == 0);
        *image = std::move(movedImage);
        CHECK(movedImage.ResourceStats().images.count == 0);
        CHECK(image->ResourceStats().images.requestedBytes == 100);
        CHECK(image->ResourceStats().images.allocationBytes == imageUsage.images.allocationBytes);
        for (int observation = 0; observation < 3; ++observation)
        {
            const auto stable = CheckSnapshot(*context->allocator);
            CHECK(stable.allocationCount == withImage.allocationCount);
            CHECK(stable.allocationBytes == withImage.allocationBytes);
            CHECK(stable.blockCount == withImage.blockCount);
            CHECK(stable.blockBytes == withImage.blockBytes);
        }

        movedAllocator = VulkanAllocator{std::move(*context->allocator)};
        CHECK_FALSE(context->allocator->QueryMemoryStats());
        const auto moved = CheckSnapshot(movedAllocator);
        CHECK(moved.allocationCount == withImage.allocationCount);
        CHECK(moved.allocationBytes == withImage.allocationBytes);
        CHECK(moved.blockCount == withImage.blockCount);
        CHECK(moved.blockBytes == withImage.blockBytes);
        *context->allocator = std::move(movedAllocator);
        CHECK_FALSE(movedAllocator.QueryMemoryStats());
        CHECK(CheckSnapshot(*context->allocator).allocationBytes == withImage.allocationBytes);

        image.reset();
        const auto releasedImage = CheckSnapshot(*context->allocator);
        CHECK(releasedImage.allocationCount == withBuffer.allocationCount);
        CHECK(releasedImage.allocationBytes == withBuffer.allocationBytes);
    }
    const auto released = CheckSnapshot(*context->allocator);
    CHECK(released.allocationCount == baseline.allocationCount);
    CHECK(released.allocationBytes == baseline.allocationBytes);
    // VMA may retain empty blocks; reserved storage is deliberately not required to be zero.
}
