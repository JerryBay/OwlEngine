#include <catch2/catch_test_macros.hpp>

#include "VulkanTexturedQuadPipeline.h"
#include <owl/vulkan/VulkanTriangle.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

TEST_CASE("Textured quad checkerboard has distinct top-left orientation marker",
          "[vulkan][texture]")
{
    const auto image = owl::vulkan::detail::MakeCheckerboardRgba8();
    constexpr std::uint32_t side = 64;
    REQUIRE(image.size() == side * side * 4);

    const auto pixel = [&](const std::uint32_t x, const std::uint32_t y)
    {
        const auto offset = (y * side + x) * 4;
        return std::array{image[offset], image[offset + 1], image[offset + 2], image[offset + 3]};
    };
    using Byte = std::byte;
    CHECK(pixel(0, 0) == std::array{Byte{0xE8}, Byte{0x35}, Byte{0x35}, Byte{0xFF}});
    CHECK(pixel(7, 7) == pixel(0, 0));
    CHECK(pixel(8, 0) == std::array{Byte{0x22}, Byte{0x29}, Byte{0x32}, Byte{0xFF}});
    CHECK(pixel(0, 8) == pixel(8, 0));
    CHECK(pixel(8, 8) == std::array{Byte{0xF4}, Byte{0xF7}, Byte{0xF9}, Byte{0xFF}});
    CHECK(pixel(63, 63) == pixel(8, 8));
}

TEST_CASE("Texture draw parameters reject invalid scale and unavailable LOD",
          "[vulkan][texture][mip]")
{
    using owl::vulkan::TextureDrawParameters;
    using owl::vulkan::detail::IsTextureDrawParametersValid;
    CHECK(IsTextureDrawParametersValid({}, 7));
    CHECK(IsTextureDrawParametersValid({.scale = 1.0F / 128, .lod = 6.0F}, 7));
    CHECK(IsTextureDrawParametersValid({.lod = 0.0F}, 1));
    CHECK_FALSE(IsTextureDrawParametersValid({.scale = 0.0F}, 7));
    CHECK_FALSE(IsTextureDrawParametersValid({.scale = 1.01F}, 7));
    CHECK_FALSE(IsTextureDrawParametersValid({.lod = -1.0F}, 7));
    CHECK_FALSE(IsTextureDrawParametersValid({.lod = 7.0F}, 7));
    CHECK_FALSE(IsTextureDrawParametersValid({.lod = 1.0F}, 1));
    CHECK_FALSE(IsTextureDrawParametersValid({}, 0));
    CHECK_FALSE(IsTextureDrawParametersValid({.scale = std::numeric_limits<float>::infinity()}, 7));
    CHECK_FALSE(IsTextureDrawParametersValid({.lod = std::numeric_limits<float>::quiet_NaN()}, 7));
}
