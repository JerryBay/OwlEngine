#include "SmokeSample.h"

#include <owl/foundation/CommandLine.h>

#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace
{
void PrintHelp()
{
    std::cout
        << "OwlSandbox\n"
        << "  --help          Show this help\n"
        << "  --sample smoke  Run the M0 SDL3 smoke sample\n";
}
}

int main(const int argc, char* argv[])
{
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index)
    {
        arguments.emplace_back(argv[index]);
    }

    const owl::foundation::ParsedCommandLine parsed =
        owl::foundation::ParseCommandLine(
            std::span<const std::string_view>{arguments});

    switch (parsed.command)
    {
    case owl::foundation::Command::Help:
        PrintHelp();
        return 0;
    case owl::foundation::Command::Smoke:
        return owl::sandbox::RunSmokeSample();
    case owl::foundation::Command::Invalid:
        std::cerr << "Error: " << parsed.error << "\n"
                  << "Run OwlSandbox --help for usage.\n";
        return 2;
    }

    return 2;
}
