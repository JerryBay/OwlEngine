#include <owl/foundation/CommandLine.h>

namespace owl::foundation
{
    ParsedCommandLine ParseCommandLine(const std::span<const std::string_view> arguments)
    {
        if (arguments.empty())
        {
            return ParsedCommandLine{.command = Command::Smoke};
        }

        if (arguments.size() == 1 && arguments[0] == "--help")
        {
            return ParsedCommandLine{.command = Command::Help};
        }

        if (arguments.size() == 1 && arguments[0] == "--sample")
        {
            return ParsedCommandLine{
                .command = Command::Invalid,
                .error = "missing sample name after --sample",
            };
        }

        if (arguments.size() == 2 && arguments[0] == "--sample" && arguments[1] == "smoke")
        {
            return ParsedCommandLine{.command = Command::Smoke};
        }

        if (arguments.size() == 2 && arguments[0] == "--sample" && arguments[1] == "clear")
        {
            return ParsedCommandLine{.command = Command::Clear};
        }

        if (arguments.size() == 2 && arguments[0] == "--sample" && arguments[1] == "triangle")
        {
            return ParsedCommandLine{.command = Command::Triangle};
        }

        return ParsedCommandLine{
            .command = Command::Invalid,
            .error = "unknown command line",
        };
    }
} // namespace owl::foundation
