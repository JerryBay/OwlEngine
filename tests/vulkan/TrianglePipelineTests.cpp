#include <catch2/catch_test_macros.hpp>

#include "VulkanTrianglePipeline.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>

TEST_CASE("Triangle memory selection respects buffer compatibility and coherency",
          "[vulkan][triangle]")
{
    VkPhysicalDeviceMemoryProperties properties{};
    properties.memoryTypeCount = 3;
    properties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    properties.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    properties.memoryTypes[2].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    using owl::vulkan::detail::SelectTriangleMemoryType;
    CHECK(SelectTriangleMemoryType(properties, 0b111) == 2);
    CHECK(SelectTriangleMemoryType(properties, 0b011) == 1);
    CHECK_FALSE(SelectTriangleMemoryType(properties, 0b001));
    CHECK_FALSE(SelectTriangleMemoryType(properties, 0));
}

TEST_CASE("Triangle SPIR-V loading rejects missing truncated and invalid binaries",
          "[vulkan][triangle]")
{
    struct Fixture
    {
        std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            ("owl-spirv-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Fixture()
        {
            std::filesystem::create_directory(directory);
        }
        ~Fixture()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } fixture;
    const auto path = fixture.directory / "test.spv";
    using owl::vulkan::detail::ReadTriangleSpirv;
    std::string error;
    CHECK_FALSE(ReadTriangleSpirv(path, error));
    CHECK_FALSE(error.empty());

    // A minimal envelope fixture; the loader is deliberately not a semantic SPIR-V validator.
    const std::array<std::uint32_t, 5> header{0x07230203, 0x00010000, 0, 1, 0};
    const auto write = [&](std::size_t count)
    {
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        file.write(reinterpret_cast<const char*>(header.data()),
                   static_cast<std::streamsize>(count));
    };
    write(sizeof(header));
    const auto loaded = ReadTriangleSpirv(path, error);
    REQUIRE(loaded);
    CHECK(loaded->size() == 5);
    CHECK((*loaded)[0] == 0x07230203);
    CHECK(error.empty());
    write(0);
    CHECK_FALSE(ReadTriangleSpirv(path, error));
    write(sizeof(header) - 1);
    CHECK_FALSE(ReadTriangleSpirv(path, error));
    {
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        const std::array<char, 20> zeros{};
        file.write(zeros.data(), zeros.size());
    }
    CHECK_FALSE(ReadTriangleSpirv(path, error));
    CHECK_FALSE(error.empty());
}
