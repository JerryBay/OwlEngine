#include "VulkanImage.h"
#include "VulkanImageView.h"
#include "VulkanSampler.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE("Image description bounds mip chains and restricts supported resources", "[vulkan][image]")
{
    using owl::vulkan::detail::IsImageDescValid;
    owl::vulkan::VulkanImageDesc desc{
        .extent = {7, 3},
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .mipLevels = 3,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };
    CHECK(IsImageDescValid(desc));
    SECTION("too many levels for non power of two extent") { desc.mipLevels = 4; }
    SECTION("zero mip levels") { desc.mipLevels = 0; }
    SECTION("zero width") { desc.extent.width = 0; }
    SECTION("zero height") { desc.extent.height = 0; }
    SECTION("undefined format") { desc.format = VK_FORMAT_UNDEFINED; }
    SECTION("depth format") { desc.format = VK_FORMAT_D32_SFLOAT; }
    SECTION("storage image") { desc.usage |= VK_IMAGE_USAGE_STORAGE_BIT; }
    SECTION("no sampled usage") { desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT; }
    CHECK_FALSE(IsImageDescValid(desc));
}

TEST_CASE("Image description handles extreme extents without mip arithmetic overflow",
          "[vulkan][image]")
{
    using owl::vulkan::detail::IsImageDescValid;
    owl::vulkan::VulkanImageDesc desc{
        .extent = {1, 1},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .mipLevels = 1,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    };
    CHECK(IsImageDescValid(desc));
    desc.mipLevels = 2;
    CHECK_FALSE(IsImageDescValid(desc));
    desc.extent = {UINT32_MAX, 1};
    desc.mipLevels = 32;
    CHECK(IsImageDescValid(desc));
    desc.mipLevels = 33;
    CHECK_FALSE(IsImageDescValid(desc));
}

TEST_CASE("Image capability policy rejects limits before native creation", "[vulkan][image]")
{
    using owl::vulkan::detail::IsImageSupported;
    const owl::vulkan::VulkanImageDesc desc{
        .extent = {16, 8},
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .mipLevels = 5,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    VkImageFormatProperties limits{
        .maxExtent = {16, 8, 1},
        .maxMipLevels = 5,
        .maxArrayLayers = 1,
        .sampleCounts = VK_SAMPLE_COUNT_1_BIT,
        .maxResourceSize = 65536,
    };
    CHECK(IsImageSupported(desc, limits));
    SECTION("width") { limits.maxExtent.width = 15; }
    SECTION("height") { limits.maxExtent.height = 7; }
    SECTION("depth") { limits.maxExtent.depth = 0; }
    SECTION("mips") { limits.maxMipLevels = 4; }
    SECTION("layers") { limits.maxArrayLayers = 0; }
    SECTION("samples") { limits.sampleCounts = VK_SAMPLE_COUNT_4_BIT; }
    CHECK_FALSE(IsImageSupported(desc, limits));
}

TEST_CASE("Image view range rejects empty out of bounds and overflowing ranges", "[vulkan][image]")
{
    using owl::vulkan::detail::IsImageViewRangeValid;
    CHECK(IsImageViewRangeValid(4, {0, 4}));
    CHECK(IsImageViewRangeValid(4, {3, 1}));
    CHECK(IsImageViewRangeValid(4, {1, 2}));
    CHECK_FALSE(IsImageViewRangeValid(0, {0, 1}));
    CHECK_FALSE(IsImageViewRangeValid(4, {0, 0}));
    CHECK_FALSE(IsImageViewRangeValid(4, {4, 1}));
    CHECK_FALSE(IsImageViewRangeValid(4, {3, 2}));
    CHECK_FALSE(IsImageViewRangeValid(4, {UINT32_MAX, 2}));
    CHECK_FALSE(IsImageViewRangeValid(4, {1, VK_REMAINING_MIP_LEVELS}));
}

TEST_CASE("Sampler policy accepts normalized sampling without optional features", "[vulkan][sampler]")
{
    using owl::vulkan::detail::IsSamplerDescValid;
    owl::vulkan::VulkanSamplerDesc desc;
    CHECK(IsSamplerDescValid(desc));
    desc.minFilter = VK_FILTER_NEAREST;
    desc.magFilter = VK_FILTER_NEAREST;
    desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.minLod = 1.0F;
    desc.maxLod = 4.0F;
    CHECK(IsSamplerDescValid(desc));
}

TEST_CASE("Sampler policy rejects unsupported modes and invalid LOD ranges", "[vulkan][sampler]")
{
    owl::vulkan::VulkanSamplerDesc desc;
    SECTION("min filter") { desc.minFilter = VK_FILTER_CUBIC_EXT; }
    SECTION("mag filter") { desc.magFilter = VK_FILTER_MAX_ENUM; }
    SECTION("mipmap mode") { desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_MAX_ENUM; }
    SECTION("U border mode") { desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; }
    SECTION("V extension mode") { desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE; }
    SECTION("W invalid mode") { desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_MAX_ENUM; }
    SECTION("negative minimum") { desc.minLod = -1.0F; }
    SECTION("negative maximum") { desc.maxLod = -1.0F; }
    SECTION("reversed range") { desc.minLod = 1.0F; }
    SECTION("NaN minimum") { desc.minLod = std::numeric_limits<float>::quiet_NaN(); }
    SECTION("NaN maximum") { desc.maxLod = std::numeric_limits<float>::quiet_NaN(); }
    SECTION("infinite minimum") { desc.minLod = std::numeric_limits<float>::infinity(); }
    SECTION("infinite maximum") { desc.maxLod = std::numeric_limits<float>::infinity(); }
    CHECK_FALSE(owl::vulkan::detail::IsSamplerDescValid(desc));
}
