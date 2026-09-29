#include "GpuTestContext.h"
#include "VulkanImage.h"
#include "VulkanImageView.h"
#include "VulkanSampler.h"

#include <catch2/catch_test_macros.hpp>
#include <owl/foundation/Log.h>

#include <array>
#include <optional>
#include <utility>

namespace
{
    using namespace owl::vulkan;

    VmaStatistics AllocationStatistics(const VulkanAllocator& allocator)
    {
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(allocator.Get(), &stats);
        return stats.total.statistics;
    }

    constexpr VulkanImageDesc ImageDesc{
        .extent = {16, 8},
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .mipLevels = 5,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };
} // namespace

TEST_CASE("Image resources reject empty parents before native calls", "[vulkan][image]")
{
    VulkanAllocator allocator;
    VulkanDevice device;
    VulkanImage image;
    std::string error;
    CHECK_FALSE(VulkanImage::Create(allocator, ImageDesc, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(VulkanImageView::Create(image, {}, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(VulkanSampler::Create(device, {}, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("Vulkan images release allocations through moves and replacement",
          "[vulkan][image][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the image resource test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto baseline = AllocationStatistics(*context->allocator);
    for (const auto format : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB})
    {
        {
            auto desc = ImageDesc;
            desc.format = format;
            auto image = VulkanImage::Create(*context->allocator, desc, error);
            INFO(error);
            REQUIRE(image);
            const VkImage original = image->Get();
            REQUIRE(original != VK_NULL_HANDLE);
            VulkanImage moved{std::move(*image)};
            CHECK_FALSE(image->IsValid());
            image.reset();
            CHECK(moved.Get() == original);
            CHECK(AllocationStatistics(*context->allocator).allocationCount ==
                  baseline.allocationCount + 1);

            desc.extent = {1, 1};
            desc.mipLevels = 1;
            auto replacement = VulkanImage::Create(*context->allocator, desc, error);
            REQUIRE(replacement);
            CHECK(AllocationStatistics(*context->allocator).allocationCount ==
                  baseline.allocationCount + 2);
            *replacement = std::move(moved);
            CHECK_FALSE(moved.IsValid());
            CHECK(replacement->Get() == original);
            CHECK(AllocationStatistics(*context->allocator).allocationCount ==
                  baseline.allocationCount + 1);
            // Uses preserved format and mip metadata, not only the moved native handle.
            auto view = VulkanImageView::Create(*replacement, {4, 1}, error);
            REQUIRE(view);
        }
        const auto released = AllocationStatistics(*context->allocator);
        CHECK(released.allocationCount == baseline.allocationCount);
        CHECK(released.allocationBytes == baseline.allocationBytes);
    }
    owl::foundation::LogMessage(owl::foundation::LogLevel::Info, "Vulkan",
        "Image allocation release verified for RGBA8 UNORM/SRGB, one and five mip levels");
}

TEST_CASE("Vulkan image views retain native identity across parent wrapper moves",
          "[vulkan][image][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the image view test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto baseline = AllocationStatistics(*context->allocator);
    // This owner must precede children even if an assertion unwinds after the move.
    std::optional<VulkanAllocator> movedAllocator;
    {
        auto image = VulkanImage::Create(*context->allocator, ImageDesc, error);
        REQUIRE(image);
        VulkanImage movedImage;
        auto full = VulkanImageView::Create(*image, {0, 5}, error);
        auto tail = VulkanImageView::Create(*image, {2, 3}, error);
        REQUIRE(full);
        REQUIRE(tail);
        const auto fullHandle = full->Get();
        CHECK_FALSE(VulkanImageView::Create(*image, {4, 2}, error));
        CHECK_FALSE(error.empty());
        CHECK(full->Get() == fullHandle);

        movedImage = std::move(*image);
        image.reset();
        movedAllocator.emplace(std::move(*context->allocator));
        CHECK_FALSE(context->allocator->IsValid());
        auto additional = VulkanImageView::Create(movedImage, {1, 1}, error);
        REQUIRE(additional);
        CHECK(error.empty());

        const auto tailHandle = tail->Get();
        VulkanImageView movedView{std::move(*tail)};
        CHECK_FALSE(tail->IsValid());
        tail.reset();
        *full = std::move(movedView);
        CHECK_FALSE(movedView.IsValid());
        CHECK(full->Get() == tailHandle);
        *context->allocator = std::move(*movedAllocator);
        // Views are destroyed here before movedImage, which precedes them in declaration order.
    }
    CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
    CHECK(AllocationStatistics(*context->allocator).allocationBytes == baseline.allocationBytes);
}

TEST_CASE("Vulkan image creation rejects invalid requests without allocating",
          "[vulkan][image][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the image rejection test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    const auto baseline = AllocationStatistics(*context->allocator);
    auto desc = ImageDesc;
    desc.extent.width = 0;
    CHECK_FALSE(VulkanImage::Create(*context->allocator, desc, error));
    CHECK_FALSE(error.empty());
    desc = ImageDesc;
    desc.extent.width = UINT32_MAX; // Structurally valid, beyond this physical device's limits.
    CHECK_FALSE(VulkanImage::Create(*context->allocator, desc, error));
    CHECK_FALSE(error.empty());
    CHECK(AllocationStatistics(*context->allocator).allocationCount == baseline.allocationCount);
    CHECK(AllocationStatistics(*context->allocator).allocationBytes == baseline.allocationBytes);
}

TEST_CASE("Vulkan samplers own sampling state independently of images",
          "[vulkan][sampler][integration]")
{
    if (!owl::tests::IsGpuTestEnabled())
        SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to run the sampler test");
    std::string error;
    auto context = owl::tests::CreateGpuContext(error);
    INFO(error);
    REQUIRE(context);
    auto source = VulkanSampler::Create(*context->device, {}, error);
    REQUIRE(source);
    REQUIRE(source->IsValid());
    const auto original = source->Get();
    VulkanSampler moved{std::move(*source)};
    CHECK_FALSE(source->IsValid());
    source.reset();
    CHECK(moved.Get() == original);

    VulkanSamplerDesc desc{
        .minFilter = VK_FILTER_NEAREST,
        .magFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .maxLod = 4.0F,
    };
    auto replacement = VulkanSampler::Create(*context->device, desc, error);
    REQUIRE(replacement);
    *replacement = std::move(moved);
    CHECK_FALSE(moved.IsValid());
    CHECK(replacement->Get() == original);
    desc.minLod = 5.0F;
    CHECK_FALSE(VulkanSampler::Create(*context->device, desc, error));
    CHECK_FALSE(error.empty());
    CHECK(replacement->Get() == original);
    // Native sampler handles may be reused for identical state; distinctness is not a contract.
}
