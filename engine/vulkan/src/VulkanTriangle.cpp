#include <owl/vulkan/VulkanTriangle.h>

#include "SDLWindowAccess.h"
#include "VulkanAllocator.h"
#include "VulkanDevice.h"
#include "VulkanFrame.h"
#include "VulkanInstance.h"
#include "VulkanSurface.h"
#include "VulkanSwapchain.h"
#include "VulkanTexturedQuadPipeline.h"
#include "VulkanTrianglePipeline.h"

#include <SDL3/SDL_video.h>
#include <owl/foundation/Log.h>
#include <owl/platform/Window.h>

#include <cstdio>
#include <exception>
#include <limits>
#include <utility>

namespace owl::vulkan
{
    struct VulkanTriangle::Impl
    {
        enum class SampleMode
        {
            Clear,
            Triangle,
            Texture,
        };

        owl::platform::Window* window = nullptr;
        VulkanInstance instance;
        VulkanSurface surface;
        VulkanDevice device;
        VulkanAllocator allocator;
        VulkanSwapchain swapchain;
        VulkanFrameResources frames;
        VulkanTrianglePipeline triangle;
        VulkanTexturedQuadPipeline texture;
        SampleMode mode = SampleMode::Clear;
        VulkanTriangleStats stats;
        VkExtent2D lastRequestedExtent{};
        std::size_t frameIndex = 0;
        bool recreateRequested = true;
        bool failed = false;
        bool deviceLost = false;
        std::string failure;

        ~Impl() noexcept
        {
            bool drained = false;
            try
            {
                std::string error;
                drained = WaitIdle(error);
            }
            catch (...)
            {
                // The native final drain below does not allocate or emit C++ diagnostics.
            }
            if (!drained && DrainForDestructionNoexcept() == DestructionDrain::Unresolved)
            {
                std::fputs("OwlEngine fatal: unresolved Vulkan work during renderer teardown\n",
                           stderr);
                std::terminate();
            }
        }

        enum class DestructionDrain
        {
            Complete,
            DeviceLost,
            Unresolved,
        };

        DestructionDrain DrainForDestructionNoexcept() noexcept
        {
            if (!device.IsValid())
                return triangle.DrainUploadForDestruction() == VK_SUCCESS &&
                               texture.DrainUploadForDestruction() == VK_SUCCESS
                           ? DestructionDrain::Complete
                           : DestructionDrain::Unresolved;
            if (deviceLost)
            {
                triangle.MarkUploadDeviceLostForDestruction();
                texture.MarkUploadDeviceLostForDestruction();
                return DestructionDrain::DeviceLost;
            }

            const auto classify = [&](const VkResult result)
            {
                if (result == VK_ERROR_DEVICE_LOST)
                {
                    deviceLost = true;
                    triangle.MarkUploadDeviceLostForDestruction();
                    texture.MarkUploadDeviceLostForDestruction();
                    return DestructionDrain::DeviceLost;
                }
                return result == VK_SUCCESS ? DestructionDrain::Complete
                                            : DestructionDrain::Unresolved;
            };

            bool unresolved = false;
            if (const auto result = triangle.DrainUploadForDestruction(); result != VK_SUCCESS)
            {
                if (classify(result) == DestructionDrain::DeviceLost)
                    return DestructionDrain::DeviceLost;
                unresolved = true;
            }
            if (const auto result = texture.DrainUploadForDestruction(); result != VK_SUCCESS)
            {
                if (classify(result) == DestructionDrain::DeviceLost)
                    return DestructionDrain::DeviceLost;
                unresolved = true;
            }

            const auto drainFence = [&](const VkFence fence, bool& pending)
            {
                if (!pending)
                    return DestructionDrain::Complete;
                if (fence == VK_NULL_HANDLE)
                    return DestructionDrain::Unresolved;
                const auto result = vkWaitForFences(
                    device.Get(), 1, &fence, VK_TRUE,
                    std::numeric_limits<std::uint64_t>::max());
                const auto status = classify(result);
                if (status == DestructionDrain::Complete)
                    pending = false;
                return status;
            };
            for (auto& slot : frames.slots)
            {
                for (bool* pending : {&slot.submissionPending, &slot.acquisitionPending})
                {
                    const VkFence fence = pending == &slot.submissionPending ? slot.complete
                                                                            : slot.acquired;
                    const auto status = drainFence(fence, *pending);
                    if (status == DestructionDrain::DeviceLost)
                        return status;
                    unresolved = unresolved || status == DestructionDrain::Unresolved;
                }
            }
            for (auto& image : frames.images)
            {
                if (image.presentPending && !device.HasPresentFences())
                {
                    unresolved = true;
                    continue;
                }
                const auto status = drainFence(image.released, image.presentPending);
                if (status == DestructionDrain::DeviceLost)
                    return status;
                unresolved = unresolved || status == DestructionDrain::Unresolved;
            }
            if (!unresolved)
                return DestructionDrain::Complete;

            const auto idleStatus = classify(vkDeviceWaitIdle(device.Get()));
            if (idleStatus != DestructionDrain::Complete)
                return idleStatus;
            // Device idle proves graphics submissions, including the startup copy and draw
            // submissions. Acquire and present fences still need their own WSI completion proof.
            triangle.MarkUploadCompleteAfterQueueIdleForDestruction();
            texture.MarkUploadCompleteAfterQueueIdleForDestruction();
            for (auto& slot : frames.slots)
            {
                slot.submissionPending = false;
                const auto status = drainFence(slot.acquired, slot.acquisitionPending);
                if (status != DestructionDrain::Complete)
                    return status;
            }
            for (auto& image : frames.images)
            {
                if (!device.HasPresentFences())
                {
                    // Existing compatibility contract: device idle is the best available proof.
                    image.presentPending = false;
                    continue;
                }
                const auto status = drainFence(image.released, image.presentPending);
                if (status != DestructionDrain::Complete)
                    return status;
            }
            return DestructionDrain::Complete;
        }

        bool Check(const VkResult result, const char* operation, std::string& error)
        {
            if (result == VK_SUCCESS)
                return true;
            failed = true;
            deviceLost = deviceLost || result == VK_ERROR_DEVICE_LOST;
            failure = std::string{operation} + " failed with VkResult " +
                      std::to_string(static_cast<int>(result));
            error = failure;
            owl::foundation::LogMessage(owl::foundation::LogLevel::Error, "Vulkan", error);
            return false;
        }

        bool WaitFence(const VkFence fence, const char* operation, std::string& error)
        {
            return Check(vkWaitForFences(device.Get(), 1, &fence, VK_TRUE,
                                         std::numeric_limits<std::uint64_t>::max()),
                         operation, error);
        }

        bool WaitSlot(VulkanFrameSlot& slot, std::string& error)
        {
            if (slot.submissionPending)
            {
                if (!WaitFence(slot.complete, "vkWaitForFences(submit)", error))
                    return false;
                slot.submissionPending = false;
            }
            if (slot.acquisitionPending)
            {
                if (!WaitFence(slot.acquired, "vkWaitForFences(acquire)", error) ||
                    !Check(vkResetFences(device.Get(), 1, &slot.acquired), "vkResetFences(acquire)",
                           error))
                    return false;
                slot.acquisitionPending = false;
            }
            return true;
        }

        bool WaitPresent(VulkanPresentImage& image, std::string& error)
        {
            if (image.presentPending && device.HasPresentFences())
            {
                if (!WaitFence(image.released, "vkWaitForFences(present)", error) ||
                    !Check(vkResetFences(device.Get(), 1, &image.released),
                           "vkResetFences(present)", error))
                    return false;
                image.presentPending = false;
            }
            return true;
        }

        bool WaitIdle(std::string& error)
        {
            error.clear();
            if (!device.IsValid())
                return true;
            if (deviceLost)
            {
                error = failure;
                return false;
            }
            auto uploadResult = triangle.WaitForUpload(error);
            if (uploadResult == VK_SUCCESS)
                uploadResult = texture.WaitForUpload(error);
            if (uploadResult != VK_SUCCESS)
            {
                failed = true;
                deviceLost = deviceLost || uploadResult == VK_ERROR_DEVICE_LOST;
                if (deviceLost)
                    failure = error;
                return false;
            }
            for (auto& slot : frames.slots)
                if (!WaitSlot(slot, error))
                    return false;
            bool anyPresent = false;
            for (auto& image : frames.images)
            {
                anyPresent = anyPresent || image.presentPending;
                if (!WaitPresent(image, error))
                    return false;
            }
            if (anyPresent && !device.HasPresentFences())
            {
                // Compatibility only: core WaitIdle does not strictly prove WSI resource release.
                if (!Check(vkDeviceWaitIdle(device.Get()), "vkDeviceWaitIdle(present fallback)",
                           error))
                    return false;
                for (auto& image : frames.images)
                    image.presentPending = false;
            }
            return true;
        }

        SwapchainUpdateResult Recreate(const VkExtent2D extent, std::string& error)
        {
            if (!WaitIdle(error))
                return SwapchainUpdateResult::Failed;
            const auto result = swapchain.CreateOrRecreate(device, surface, extent, error);
            if (result != SwapchainUpdateResult::Ready)
            {
                if (result == SwapchainUpdateResult::Failed)
                {
                    failed = true;
                    failure = error;
                }
                return result;
            }
            // Deferred/preflight failure preserves the old chain and its matching sync objects.
            if (mode == SampleMode::Triangle &&
                !triangle.SetColorFormat(swapchain.SurfaceFormat().format, error))
            {
                failed = true;
                failure = error;
                return SwapchainUpdateResult::Failed;
            }
            if (mode == SampleMode::Texture &&
                !texture.SetColorFormat(swapchain.SurfaceFormat().format, error))
            {
                failed = true;
                failure = error;
                return SwapchainUpdateResult::Failed;
            }
            frames.ResetPresentResources();
            if (!frames.CreatePresentResources(swapchain.Images().size(), device.HasPresentFences(),
                                               error))
            {
                failed = true;
                failure = error;
                return SwapchainUpdateResult::Failed;
            }
            lastRequestedExtent = extent;
            stats.width = swapchain.Extent().width;
            stats.height = swapchain.Extent().height;
            ++stats.swapchainGeneration;
            recreateRequested = false;
            return SwapchainUpdateResult::Ready;
        }

        bool RecordFrame(const VulkanFrameSlot& slot, const std::uint32_t imageIndex,
                         std::string& error)
        {
            if (!Check(vkResetCommandPool(device.Get(), slot.commandPool, 0), "vkResetCommandPool",
                       error))
                return false;
            const VkCommandBufferBeginInfo begin{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };
            if (!Check(vkBeginCommandBuffer(slot.commandBuffer, &begin), "vkBeginCommandBuffer",
                       error))
                return false;
            // Match the acquire semaphore wait stage so the layout transition happens after it.
            // Discarding the previous contents does not remove this execution dependency.
            VkImageMemoryBarrier2 barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain.Images()[imageIndex],
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            };
            const VkDependencyInfo dependency{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier,
            };
            vkCmdPipelineBarrier2(slot.commandBuffer, &dependency);
            const VkRenderingAttachmentInfo attachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = swapchain.ImageViews()[imageIndex],
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {{0.02F, 0.35F, 0.18F, 1.0F}}},
            };
            const VkRenderingInfo rendering{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {{0, 0}, swapchain.Extent()},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &attachment,
            };
            vkCmdBeginRendering(slot.commandBuffer, &rendering);
            if (mode == SampleMode::Triangle)
                triangle.RecordDraw(slot.commandBuffer, swapchain.Extent());
            else if (mode == SampleMode::Texture)
                texture.RecordDraw(slot.commandBuffer, swapchain.Extent());
            vkCmdEndRendering(slot.commandBuffer);
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            barrier.dstAccessMask = VK_ACCESS_2_NONE;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier2(slot.commandBuffer, &dependency);
            return Check(vkEndCommandBuffer(slot.commandBuffer), "vkEndCommandBuffer", error);
        }

        FrameResult RenderFrame(std::string& error)
        {
            error.clear();
            if (failed)
            {
                error = failure;
                return FrameResult::Failed;
            }
            const auto pixels = window->GetFramebufferExtent();
            SDL_Window* native = owl::platform::SDLWindowAccess::Get(*window);
            if (!pixels || native == nullptr)
            {
                failed = true;
                failure = error = "Cannot query the renderer window framebuffer extent";
                return FrameResult::Failed;
            }
            if ((SDL_GetWindowFlags(native) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0 ||
                pixels->width <= 0 || pixels->height <= 0)
            {
                recreateRequested = true;
                return FrameResult::Deferred;
            }
            const VkExtent2D extent{static_cast<std::uint32_t>(pixels->width),
                                    static_cast<std::uint32_t>(pixels->height)};
            if (extent.width != lastRequestedExtent.width ||
                extent.height != lastRequestedExtent.height)
                recreateRequested = true;
            if (recreateRequested)
            {
                const auto result = Recreate(extent, error);
                if (result == SwapchainUpdateResult::Deferred)
                    return FrameResult::Deferred;
                if (result == SwapchainUpdateResult::Failed)
                    return FrameResult::Failed;
            }

            auto& slot = frames.slots[frameIndex];
            if (!WaitSlot(slot, error))
                return FrameResult::Failed;
            std::uint32_t imageIndex = 0;
            constexpr std::uint64_t AcquireTimeoutNs = 16'000'000;
            const VkResult acquire =
                vkAcquireNextImageKHR(device.Get(), swapchain.Get(), AcquireTimeoutNs,
                                      slot.imageAvailable, slot.acquired, &imageIndex);
            switch (detail::ClassifyAcquireResult(acquire))
            {
            case detail::AcquireAction::Recreate:
                recreateRequested = true;
                return FrameResult::Deferred;
            case detail::AcquireAction::Defer:
                return FrameResult::Deferred;
            case detail::AcquireAction::Fail:
                static_cast<void>(Check(acquire, "vkAcquireNextImageKHR", error));
                return FrameResult::Failed;
            case detail::AcquireAction::Render:
                break;
            }
            slot.acquisitionPending = true;
            recreateRequested = acquire == VK_SUBOPTIMAL_KHR;
            auto& image = frames.images[imageIndex];
            if (!WaitPresent(image, error) || !RecordFrame(slot, imageIndex, error))
                return FrameResult::Failed;

            const VkSemaphoreSubmitInfo wait{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = slot.imageAvailable,
                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
            const VkSemaphoreSubmitInfo signal{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = image.renderFinished,
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            };
            const VkCommandBufferSubmitInfo command{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = slot.commandBuffer,
            };
            const VkSubmitInfo2 submit{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .waitSemaphoreInfoCount = 1,
                .pWaitSemaphoreInfos = &wait,
                .commandBufferInfoCount = 1,
                .pCommandBufferInfos = &command,
                .signalSemaphoreInfoCount = 1,
                .pSignalSemaphoreInfos = &signal,
            };
            // Only reset now: acquire retry and recording errors must not strand this fence.
            if (!Check(vkResetFences(device.Get(), 1, &slot.complete), "vkResetFences(submit)",
                       error) ||
                !Check(vkQueueSubmit2(device.GraphicsQueue(), 1, &submit, slot.complete),
                       "vkQueueSubmit2", error))
                return FrameResult::Failed;
            slot.submissionPending = true;
            ++stats.submittedFrames;
            if (mode != SampleMode::Clear)
                ++stats.indexedDraws;

            const VkSwapchainKHR chain = swapchain.Get();
            const VkSwapchainPresentFenceInfoKHR presentFence{
                .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR,
                .swapchainCount = 1,
                .pFences = &image.released,
            };
            const VkPresentInfoKHR present{
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .pNext = device.HasPresentFences() ? &presentFence : nullptr,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &image.renderFinished,
                .swapchainCount = 1,
                .pSwapchains = &chain,
                .pImageIndices = &imageIndex,
            };
            const VkResult result = vkQueuePresentKHR(device.PresentQueue(), &present);
            const auto decision = detail::ClassifyPresentResult(result);
            image.presentPending = decision.enqueued;
            recreateRequested = recreateRequested || decision.recreate;
            frameIndex = (frameIndex + 1) % frames.slots.size();
            if (decision.failed)
            {
                static_cast<void>(Check(result, "vkQueuePresentKHR", error));
                return FrameResult::Failed;
            }
            if (result == VK_ERROR_OUT_OF_DATE_KHR)
                return FrameResult::Deferred;
            ++stats.presentedFrames;
            return FrameResult::Presented;
        }
    };
    VulkanTriangle::VulkanTriangle() noexcept = default;
    VulkanTriangle::~VulkanTriangle() = default;
    VulkanTriangle::VulkanTriangle(VulkanTriangle&&) noexcept = default;
    VulkanTriangle& VulkanTriangle::operator=(VulkanTriangle&&) noexcept = default;
    VulkanTriangle::VulkanTriangle(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
    std::optional<VulkanTriangle> VulkanTriangle::Create(owl::platform::Window& window,
                                                         std::string& error,
                                                         const VulkanTriangleOptions options)
    {
        if (options.triangleShaders && options.textureShaders)
        {
            error = "Cannot select triangle and texture samples simultaneously";
            return std::nullopt;
        }
        SDL_Window* native = owl::platform::SDLWindowAccess::Get(window);
        if (!window.IsValid() || native == nullptr ||
            (SDL_GetWindowFlags(native) & SDL_WINDOW_VULKAN) == 0)
        {
            error = "Vulkan renderer requires a valid Vulkan-capable window";
            return std::nullopt;
        }
        auto impl = std::make_unique<Impl>();
        impl->window = &window;
        auto instance = VulkanInstance::Create(error);
        if (!instance)
            return std::nullopt;
        impl->instance = std::move(*instance);
        auto surface = VulkanSurface::Create(impl->instance, window, error);
        if (!surface)
            return std::nullopt;
        impl->surface = std::move(*surface);
        const auto selection =
            VulkanDeviceSelection::Select(impl->instance.Get(), impl->surface.Get(), error);
        if (!selection)
            return std::nullopt;
        const auto presentationSupport = options.enablePresentFences
                                             ? impl->instance.PresentationSupport()
                                             : detail::PresentationSupportCapabilities{};
        auto device = VulkanDevice::Create(*selection, error, presentationSupport);
        if (!device)
            return std::nullopt;
        impl->device = std::move(*device);
        if (options.triangleShaders || options.textureShaders)
        {
            auto allocator = VulkanAllocator::Create(impl->instance.Get(), impl->device, error);
            if (!allocator)
                return std::nullopt;
            impl->allocator = std::move(*allocator);
            if (options.triangleShaders)
            {
                if (!impl->triangle.Initialize(impl->device, impl->allocator,
                                               *options.triangleShaders, error))
                    return std::nullopt;
                impl->mode = Impl::SampleMode::Triangle;
            }
            else
            {
                if (!impl->texture.Initialize(impl->device, impl->allocator,
                                              *options.textureShaders, error))
                    return std::nullopt;
                impl->mode = Impl::SampleMode::Texture;
            }
        }
        if (!impl->frames.Initialize(impl->device.Get(),
                                     *impl->device.QueueFamilies().graphicsFamily, error))
            return std::nullopt;
        impl->stats.presentFencesEnabled = impl->device.HasPresentFences();
        impl->stats.validationEnabled = impl->instance.ValidationEnabled();
        owl::foundation::LogMessage(
            impl->device.HasPresentFences() ? owl::foundation::LogLevel::Info
                                            : owl::foundation::LogLevel::Warning,
            "Vulkan",
            impl->device.HasPresentFences()
                ? "Renderer uses per-image present fences for resource release"
                : "Renderer uses WaitIdle compatibility fallback for WSI cleanup; strict "
                  "presentation resource release is not guaranteed");
        error.clear();
        return VulkanTriangle{std::move(impl)};
    }
    bool VulkanTriangle::IsValid() const noexcept
    {
        return impl_ != nullptr && !impl_->failed;
    }
    FrameResult VulkanTriangle::RenderFrame(std::string& error)
    {
        if (!impl_)
        {
            error = "Cannot render with an empty Vulkan renderer";
            return FrameResult::Failed;
        }
        return impl_->RenderFrame(error);
    }
    bool VulkanTriangle::WaitIdle(std::string& error)
    {
        error.clear();
        return !impl_ || impl_->WaitIdle(error);
    }
    VulkanTriangleStats VulkanTriangle::Stats() const noexcept
    {
        return impl_ ? impl_->stats : VulkanTriangleStats{};
    }
} // namespace owl::vulkan
