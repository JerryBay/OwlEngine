#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace owl::vulkan::detail
{
    // Checks the binary envelope, not full SPIR-V semantics; use spirv-val for asset validation.
    [[nodiscard]] std::optional<std::vector<std::uint32_t>>
    ReadSampleSpirv(const std::filesystem::path& path, std::string& error);
} // namespace owl::vulkan::detail
