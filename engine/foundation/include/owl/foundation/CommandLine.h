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
    Invalid,
};

struct ParsedCommandLine
{
    Command command = Command::Invalid;
    std::string error;
};

[[nodiscard]] ParsedCommandLine ParseCommandLine(
    std::span<const std::string_view> arguments);
}
