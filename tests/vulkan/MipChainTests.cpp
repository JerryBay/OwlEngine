#include "VulkanMipChain.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace owl::vulkan;

    VulkanImageDesc MipDesc(VkExtent2D extent, VkFormat format, std::uint32_t levels)
    {
        return {.extent = extent,
                .format = format,
                .mipLevels = levels,
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    }
} // namespace

TEST_CASE("CPU mip filtering includes every pixel of odd source dimensions", "[vulkan][mip]")
{
    std::array<std::byte, 7 * 3 * 4> base{};
    for (std::size_t y = 0; y < 3; ++y)
    {
        for (std::size_t x = 0; x < 7; ++x)
        {
            const auto index = (y * 7 + x) * 4;
            base[index] = static_cast<std::byte>(x * 10);
            base[index + 1] = static_cast<std::byte>(y * 20);
            base[index + 2] = std::byte{40};
            base[index + 3] = std::byte{255};
        }
    }
    std::string error;
    const auto chain = detail::BuildCpuMipChain(
        MipDesc({7, 3}, VK_FORMAT_R8G8B8A8_UNORM, 3), base, error);
    INFO(error);
    REQUIRE(chain);
    REQUIRE(chain->levels.size() == 3);
    CHECK(chain->levels[0].extent.width == 7);
    CHECK(chain->levels[1].extent.width == 3);
    CHECK(chain->levels[1].extent.height == 1);
    CHECK(chain->levels[2].extent.width == 1);
    CHECK(chain->levels[2].extent.height == 1);
    CHECK(chain->bytes.size() == (21 + 3 + 1) * 4);
    const auto offset = chain->levels[1].offset;
    CHECK(chain->bytes[offset] == std::byte{7});
    CHECK(chain->bytes[offset + 4] == std::byte{30});
    CHECK(chain->bytes[offset + 8] == std::byte{53});
    CHECK(chain->bytes[offset + 1] == std::byte{20});
    CHECK(chain->bytes[chain->levels[2].offset] == std::byte{30});
}

TEST_CASE("CPU sRGB mip filtering averages RGB in linear light and alpha directly", "[vulkan][mip]")
{
    constexpr std::array<std::byte, 16> base{
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    std::string error;
    const auto srgb = detail::BuildCpuMipChain(
        MipDesc({2, 2}, VK_FORMAT_R8G8B8A8_SRGB, 2), base, error);
    INFO(error);
    REQUIRE(srgb);
    const auto offset = srgb->levels[1].offset;
    CHECK(srgb->bytes[offset] == std::byte{188});
    CHECK(srgb->bytes[offset + 1] == std::byte{188});
    CHECK(srgb->bytes[offset + 2] == std::byte{188});
    CHECK(srgb->bytes[offset + 3] == std::byte{128});
    const auto unorm = detail::BuildCpuMipChain(
        MipDesc({2, 2}, VK_FORMAT_R8G8B8A8_UNORM, 2), base, error);
    REQUIRE(unorm);
    CHECK(unorm->bytes[unorm->levels[1].offset] == std::byte{128});
}

TEST_CASE("CPU mip generator rejects malformed base input", "[vulkan][mip]")
{
    std::array<std::byte, 16> base{};
    std::string error;
    CHECK_FALSE(detail::BuildCpuMipChain(
        MipDesc({2, 2}, VK_FORMAT_R8G8B8A8_UNORM, 2),
        std::span{base}.first(15), error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(detail::BuildCpuMipChain(
        MipDesc({2, 2}, VK_FORMAT_R8G8B8A8_UNORM, 3), base, error));
    CHECK_FALSE(error.empty());
}
