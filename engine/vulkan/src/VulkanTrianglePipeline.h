#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace owl::vulkan::detail
{
    // Filter by the buffer's memoryTypeBits before preferring coherent host-visible memory.
    [[nodiscard]] std::optional<std::uint32_t>
    SelectTriangleMemoryType(const VkPhysicalDeviceMemoryProperties& properties,
                             std::uint32_t memoryTypeBits) noexcept;

    // Checks the binary envelope, not full SPIR-V semantics; use spirv-val for asset validation.
    [[nodiscard]] std::optional<std::vector<std::uint32_t>>
    ReadTriangleSpirv(const std::filesystem::path& path, std::string& error);
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    class VulkanDevice;
    struct TriangleShaderPaths;

    // Borrows Device. Caller finishes all submitted draws before destruction or format changes.
    // Owns one immutable vertex/index buffer, its memory, and a format-dependent graphics pipeline.
    class VulkanTrianglePipeline
    {
    public:
        VulkanTrianglePipeline() = default;
        ~VulkanTrianglePipeline();
        VulkanTrianglePipeline(const VulkanTrianglePipeline&) = delete;
        VulkanTrianglePipeline& operator=(const VulkanTrianglePipeline&) = delete;

        [[nodiscard]] bool Initialize(const VulkanDevice& device, const TriangleShaderPaths& paths,
                                      std::string& error);
        [[nodiscard]] bool SetColorFormat(VkFormat format, std::string& error);
        // Requires a successful SetColorFormat and an active matching Dynamic Rendering scope.
        void RecordDraw(VkCommandBuffer command, VkExtent2D extent) const noexcept;

    private:
        VkDevice device_ = VK_NULL_HANDLE;
        VkBuffer geometry_ = VK_NULL_HANDLE;
        VkDeviceMemory memory_ = VK_NULL_HANDLE;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        VkFormat format_ = VK_FORMAT_UNDEFINED;
        std::vector<std::uint32_t> vertexShader_;
        std::vector<std::uint32_t> fragmentShader_;
    };
} // namespace owl::vulkan
