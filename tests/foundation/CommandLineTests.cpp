#include <owl/foundation/CommandLine.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <string_view>

using owl::foundation::Command;
using owl::foundation::ParseCommandLine;

namespace
{
    struct SampleCommand
    {
        std::string_view name;
        Command command;
    };

    constexpr std::array SampleCommands{
        SampleCommand{"smoke", Command::Smoke},
        SampleCommand{"clear", Command::Clear},
        SampleCommand{"triangle", Command::Triangle},
        SampleCommand{"texture", Command::Texture},
        SampleCommand{"texture-mips", Command::TextureMips},
        SampleCommand{"texture-mips-cpu", Command::TextureMipsCpu},
    };
} // namespace

TEST_CASE("No arguments selects smoke", "[command-line]")
{
    const auto parsed = ParseCommandLine(std::span<const std::string_view>{});
    CHECK(parsed.command == Command::Smoke);
    CHECK(parsed.error.empty());
}

TEST_CASE("Help option selects help", "[command-line]")
{
    constexpr std::array arguments{std::string_view{"--help"}};
    const auto parsed = ParseCommandLine(arguments);
    CHECK(parsed.command == Command::Help);
    CHECK(parsed.error.empty());
}

TEST_CASE("Sample names select distinct commands", "[command-line]")
{
    for (const auto& sample : SampleCommands)
    {
        CAPTURE(sample.name);
        const std::array arguments{std::string_view{"--sample"}, sample.name};
        const auto parsed = ParseCommandLine(arguments);
        CHECK(parsed.command == sample.command);
        CHECK(parsed.error.empty());
    }
}

TEST_CASE("Missing sample name is invalid", "[command-line]")
{
    constexpr std::array arguments{std::string_view{"--sample"}};
    const auto parsed = ParseCommandLine(arguments);
    CHECK(parsed.command == Command::Invalid);
    CHECK(parsed.error == "missing sample name after --sample");
}

TEST_CASE("Samples reject trailing arguments", "[command-line]")
{
    for (const auto& sample : SampleCommands)
    {
        CAPTURE(sample.name);
        const std::array arguments{std::string_view{"--sample"}, sample.name,
                                   std::string_view{"unexpected"}};
        const auto parsed = ParseCommandLine(arguments);
        CHECK(parsed.command == Command::Invalid);
        CHECK_FALSE(parsed.error.empty());
    }
}

TEST_CASE("Unknown sample names are invalid", "[command-line]")
{
    constexpr std::array arguments{
        std::string_view{"--sample"},
        std::string_view{"unknown"},
    };
    const auto parsed = ParseCommandLine(arguments);
    CHECK(parsed.command == Command::Invalid);
    CHECK_FALSE(parsed.error.empty());
}

TEST_CASE("Unknown arguments are invalid", "[command-line]")
{
    constexpr std::array arguments{std::string_view{"--unknown"}};
    const auto parsed = ParseCommandLine(arguments);
    CHECK(parsed.command == Command::Invalid);
    CHECK(parsed.error == "unknown command line");
}
