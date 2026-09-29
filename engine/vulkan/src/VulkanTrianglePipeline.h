#pragma once

#include "VulkanBuffer.h"
#include "VulkanBufferUpload.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace owl::vulkan::detail
{
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

        [[nodiscard]] bool Initialize(const VulkanDevice& device, const VulkanAllocator& allocator,
                                      const TriangleShaderPaths& paths,
                                      std::string& error);
        [[nodiscard]] bool SetColorFormat(VkFormat format, std::string& error);
        // Completes the startup geometry upload and installs the device-local destination.
        // Calling this repeatedly is safe after successful completion.
        [[nodiscard]] VkResult WaitForUpload(std::string& error);
        [[nodiscard]] VkResult DrainUploadForDestruction() noexcept;
        void MarkUploadCompleteAfterQueueIdleForDestruction() noexcept;
        void MarkUploadDeviceLostForDestruction() noexcept;
        // Requires a successful SetColorFormat and an active matching Dynamic Rendering scope.
        void RecordDraw(VkCommandBuffer command, VkExtent2D extent) const noexcept;

    private:
        VkDevice device_ = VK_NULL_HANDLE;
        VulkanBuffer geometry_;
        std::optional<VulkanBufferUpload> upload_;
        VkPipelineLayout layout_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        VkFormat format_ = VK_FORMAT_UNDEFINED;
        std::vector<std::uint32_t> vertexShader_;
        std::vector<std::uint32_t> fragmentShader_;
    };
} // namespace owl::vulkan
