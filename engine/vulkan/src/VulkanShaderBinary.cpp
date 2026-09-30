#include "VulkanShaderBinary.h"

#include <fstream>

namespace owl::vulkan::detail
{
    std::optional<std::vector<std::uint32_t>> ReadSampleSpirv(const std::filesystem::path& path,
                                                              std::string& error)
    {
        const auto utf8 = path.u8string();
        const std::string name{utf8.begin(), utf8.end()};
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file)
        {
            error = "Cannot open sample SPIR-V: " + name;
            return std::nullopt;
        }
        const auto bytes = file.tellg();
        if (bytes < 20 || bytes > 4 * 1024 * 1024 || bytes % 4 != 0)
        {
            error = "Invalid sample SPIR-V byte size: " + name;
            return std::nullopt;
        }
        std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes) / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(bytes));
        if (!file || words[0] != 0x07230203 || words[1] < 0x00010000 || words[1] > 0x00010600 ||
            words[3] == 0 || words[4] != 0)
        {
            error = "Invalid sample SPIR-V header or incomplete read: " + name;
            return std::nullopt;
        }
        error.clear();
        return words;
    }
} // namespace owl::vulkan::detail
