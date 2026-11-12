#include "VulkanImGuiRenderer.h"

#include "VulkanDevice.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <cstdio>
#include <cstdlib>

namespace owl::vulkan
{
    namespace
    {
        class ContextScope
        {
        public:
            explicit ContextScope(ImGuiContext* context) : previous_(ImGui::GetCurrentContext())
            {
                ImGui::SetCurrentContext(context);
            }
            ~ContextScope()
            {
                ImGui::SetCurrentContext(previous_);
            }

        private:
            ImGuiContext* previous_;
        };

        void CheckBackendResult(const VkResult result)
        {
            if (result == VK_SUCCESS)
                return;
            // The unmodified backend does not unwind partially created native resources.
            // Returning or throwing here would continue invalid work or leak live children.
            std::fprintf(stderr, "OwlEngine fatal: ImGui Vulkan backend failed with VkResult %d\n",
                         static_cast<int>(result));
            std::fflush(stderr);
            std::abort();
        }

        ImGui_ImplVulkan_PipelineInfo PipelineInfo(const VkFormat* format)
        {
            ImGui_ImplVulkan_PipelineInfo info{};
            info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            info.PipelineRenderingCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                .colorAttachmentCount = 1,
                .pColorAttachmentFormats = format,
            };
            return info;
        }
    } // namespace

    VulkanImGuiRenderer::~VulkanImGuiRenderer()
    {
        if (initialized_)
        {
            const ContextScope context{context_};
            ImGui_ImplVulkan_Shutdown();
        }
        if (pool_ != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device_, pool_, nullptr);
    }

    bool VulkanImGuiRenderer::Attach(ImGuiContext* context,
                                     const VulkanDebugUiAccess::BuildFrame builder, void* userdata,
                                     std::string& error)
    {
        if (context_ != nullptr || context == nullptr || builder == nullptr)
        {
            error = "Debug UI requires one context and a frame builder, attached once";
            return false;
        }
        const ContextScope scope{context};
        if (ImGui::GetIO().BackendRendererUserData != nullptr)
        {
            error = "Debug UI context already has a renderer backend";
            return false;
        }
        context_ = context;
        builder_ = builder;
        userdata_ = userdata;
        error.clear();
        return true;
    }

    bool VulkanImGuiRenderer::IsAttached() const noexcept
    {
        return context_ != nullptr;
    }

    VkResult VulkanImGuiRenderer::SetColorFormat(const VkInstance instance,
                                                 const VulkanDevice& device, const VkFormat format,
                                                 const std::uint32_t frameCount)
    {
        if (!IsAttached() || (initialized_ && format_ == format))
            return VK_SUCCESS;
        const ContextScope context{context_};
        format_ = format;
        const auto pipeline = PipelineInfo(&format_);
        if (initialized_)
        {
            ImGui_ImplVulkan_CreateMainPipeline(&pipeline);
            return VK_SUCCESS;
        }

        device_ = device.Get();
        constexpr std::array sizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                 IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER,
                                 IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE},
        };
        const VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = sizes[0].descriptorCount + sizes[1].descriptorCount,
            .poolSizeCount = static_cast<std::uint32_t>(sizes.size()),
            .pPoolSizes = sizes.data(),
        };
        VkDescriptorPool pool = VK_NULL_HANDLE;
        const auto result = vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool);
        if (result != VK_SUCCESS)
            return result;
        pool_ = pool;

        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_3;
        info.Instance = instance;
        info.PhysicalDevice = device.PhysicalDevice();
        info.Device = device_;
        info.QueueFamily = *device.QueueFamilies().graphicsFamily;
        info.Queue = device.GraphicsQueue();
        info.DescriptorPool = pool_;
        // This is the backend's draw-buffer ring, tied to fenced slots, not WSI images.
        info.MinImageCount = frameCount;
        info.ImageCount = frameCount;
        info.PipelineInfoMain = pipeline;
        info.UseDynamicRendering = true;
        info.CheckVkResultFn = CheckBackendResult;
        initialized_ = ImGui_ImplVulkan_Init(&info);
        return initialized_ ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
    }

    bool VulkanImGuiRenderer::BuildFrame(std::string& error)
    {
        const ContextScope context{context_};
        ImGui_ImplVulkan_NewFrame();
        builder_(userdata_);
        const auto* draw = ImGui::GetDrawData();
        if (draw == nullptr || !draw->Valid ||
            draw->DisplaySize.x * draw->FramebufferScale.x < 1.0F ||
            draw->DisplaySize.y * draw->FramebufferScale.y < 1.0F)
        {
            error = "Debug UI builder must finish a drawable ImGui frame";
            return false;
        }
        return true;
    }

    void VulkanImGuiRenderer::RecordDraw(const VkCommandBuffer commandBuffer)
    {
        const ContextScope context{context_};
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }
} // namespace owl::vulkan
