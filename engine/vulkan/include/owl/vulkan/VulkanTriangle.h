#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace owl::platform
{
    class Window;
}

namespace owl::vulkan
{
    enum class FrameResult
    {
        Presented,
        Deferred,
        Failed
    };

    struct TriangleShaderPaths
    {
        std::filesystem::path vertex;
        std::filesystem::path fragment;
    };

    enum class TextureMipMode
    {
        SingleLevel,
        Generate,
        ForceCpu,
    };

    // Sample-local draw controls. Absence of LOD requests the shader's implicit derivatives.
    struct TextureDrawParameters
    {
        float scale = 1.0F;
        std::optional<float> lod;
    };

    struct VulkanTriangleOptions
    {
        // Disable to exercise the Vulkan 1.3 compatibility path for diagnostics.
        bool enablePresentFences = true;
        // Absent keeps the original clear-only path. Shader bytes are loaded during Create.
        std::optional<TriangleShaderPaths> triangleShaders;
        // Selects the textured quad sample instead. Mutually exclusive with triangleShaders.
        std::optional<TriangleShaderPaths> textureShaders;
        // Generated modes require the sample mip shaders' scale/LOD push-constant interface.
        TextureMipMode textureMipMode = TextureMipMode::SingleLevel;
    };

    struct VulkanTriangleStats
    {
        std::uint64_t submittedFrames = 0;
        std::uint64_t presentedFrames = 0;
        std::uint64_t indexedDraws = 0;
        std::uint32_t swapchainGeneration = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool presentFencesEnabled = false;
        bool validationEnabled = false;
        std::uint32_t textureMipLevels = 0;
        bool textureGpuMipGeneration = false;
        // Renderer graphics-submit domain. Completion does not prove present/scanout release.
        std::uint64_t submittedSerial = 0;
        std::uint64_t completedSerial = 0;
    };

    // Sample lifecycle. Clears, then optionally draws one configured indexed sample.
    // Window must remain alive and unmoved. All calls and destruction are single-threaded
    // on the window thread. The caller pumps events and throttles Deferred frames.
    // Destruction/move replacement waits for owned work; use WaitIdle for error reporting.
    // Strict WSI resource-release proof requires present fences; the fallback is best effort.
    class VulkanTriangle
    {
    public:
        VulkanTriangle() noexcept;
        ~VulkanTriangle();
        VulkanTriangle(const VulkanTriangle&) = delete;
        VulkanTriangle& operator=(const VulkanTriangle&) = delete;
        VulkanTriangle(VulkanTriangle&&) noexcept;
        VulkanTriangle& operator=(VulkanTriangle&&) noexcept;

        [[nodiscard]] static std::optional<VulkanTriangle>
        Create(owl::platform::Window& window, std::string& error,
               VulkanTriangleOptions options = {});
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] FrameResult RenderFrame(std::string& error,
                                              const TextureDrawParameters& parameters = {});
        [[nodiscard]] bool WaitIdle(std::string& error);
        [[nodiscard]] VulkanTriangleStats Stats() const noexcept;

    private:
        struct Impl;
        explicit VulkanTriangle(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> impl_;
    };
} // namespace owl::vulkan
