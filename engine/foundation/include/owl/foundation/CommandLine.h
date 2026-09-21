#pragma once

#include <span>
#include <string>
#include <string_view>

namespace owl::foundation
{
    enum class Command
    {
        Help,
        Smoke,
        Clear,
        Triangle,
        Invalid,
    };

    struct ParsedCommandLine
    {
        Command command = Command::Invalid;
        std::string error;
    };

    [[nodiscard]] ParsedCommandLine ParseCommandLine(std::span<const std::string_view> arguments);
} // namespace owl::foundation
