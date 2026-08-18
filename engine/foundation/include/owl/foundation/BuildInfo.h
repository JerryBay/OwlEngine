#pragma once

#include <string_view>

namespace owl::foundation
{
struct BuildInfo
{
    std::string_view projectVersion;
    std::string_view gitRevision;
    std::string_view buildConfiguration;
    std::string_view compiler;
    std::string_view operatingSystem;
    std::string_view architecture;
};

[[nodiscard]] BuildInfo GetBuildInfo() noexcept;
} // namespace owl::foundation
