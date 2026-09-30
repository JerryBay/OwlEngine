#include <catch2/catch_test_macros.hpp>

#include "VulkanShaderBinary.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>

TEST_CASE("Sample SPIR-V loading rejects missing truncated and invalid binaries",
          "[vulkan][shader]")
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
    using owl::vulkan::detail::ReadSampleSpirv;
    std::string error;
    CHECK_FALSE(ReadSampleSpirv(path, error));
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
    const auto loaded = ReadSampleSpirv(path, error);
    REQUIRE(loaded);
    CHECK(loaded->size() == 5);
    CHECK((*loaded)[0] == 0x07230203);
    CHECK(error.empty());
    write(0);
    CHECK_FALSE(ReadSampleSpirv(path, error));
    write(sizeof(header) - 1);
    CHECK_FALSE(ReadSampleSpirv(path, error));
    {
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        const std::array<char, 20> zeros{};
        file.write(zeros.data(), zeros.size());
    }
    CHECK_FALSE(ReadSampleSpirv(path, error));
    CHECK_FALSE(error.empty());
}
