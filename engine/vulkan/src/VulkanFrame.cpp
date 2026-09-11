#include "VulkanFrame.h"

#include <owl/foundation/Log.h>

namespace owl::vulkan
{
    namespace detail
    {
        AcquireAction ClassifyAcquireResult(const VkResult result) noexcept
        {
            switch (result)
            {
            case VK_SUCCESS:
            case VK_SUBOPTIMAL_KHR:
                return AcquireAction::Render;
            case VK_ERROR_OUT_OF_DATE_KHR:
                return AcquireAction::Recreate;
            case VK_TIMEOUT:
            case VK_NOT_READY:
                return AcquireAction::Defer;
            default:
                return AcquireAction::Fail;
            }
        }

        PresentDecision ClassifyPresentResult(const VkResult result) noexcept
        {
            switch (result)
            {
            case VK_SUCCESS:
                return {true, false, false};
            case VK_SUBOPTIMAL_KHR:
            case VK_ERROR_OUT_OF_DATE_KHR:
                return {true, true, false};
            case VK_ERROR_SURFACE_LOST_KHR:
            case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
            case VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT:
                return {true, false, true};
            default:
                return {false, false, true};
            }
        }
    } // namespace detail

    namespace
    {
        bool Check(const VkResult result, const char* operation, std::string& error)
        {
            if (result == VK_SUCCESS)
                return true;
            error = std::string{operation} + " failed with VkResult " +
                    std::to_string(static_cast<int>(result));
            owl::foundation::LogMessage(owl::foundation::LogLevel::Error, "Vulkan", error);
            return false;
        }

        bool CreateSemaphore(const VkDevice device, VkSemaphore& destination, std::string& error)
        {
            const VkSemaphoreCreateInfo info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkSemaphore handle = VK_NULL_HANDLE;
            if (!Check(vkCreateSemaphore(device, &info, nullptr, &handle), "vkCreateSemaphore",
                       error))
                return false;
            destination = handle;
            return true;
        }

        bool CreateFence(const VkDevice device, const VkFenceCreateFlags flags,
                         VkFence& destination, std::string& error)
        {
            const VkFenceCreateInfo info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                         .flags = flags};
            VkFence handle = VK_NULL_HANDLE;
            if (!Check(vkCreateFence(device, &info, nullptr, &handle), "vkCreateFence", error))
                return false;
            destination = handle;
            return true;
        }
    } // namespace

    VulkanFrameResources::~VulkanFrameResources()
    {
        if (device_ == VK_NULL_HANDLE)
            return;
        ResetPresentResources();
        for (const auto& slot : slots)
        {
            vkDestroyFence(device_, slot.acquired, nullptr);
            vkDestroyFence(device_, slot.complete, nullptr);
            vkDestroySemaphore(device_, slot.imageAvailable, nullptr);
            vkDestroyCommandPool(device_, slot.commandPool, nullptr);
        }
    }

    bool VulkanFrameResources::Initialize(const VkDevice device, const std::uint32_t graphicsFamily,
                                          std::string& error)
    {
        if (device == VK_NULL_HANDLE || device_ != VK_NULL_HANDLE)
        {
            error = "Frame resources require a valid device and an empty owner";
            return false;
        }
        device_ = device;
        for (auto& slot : slots)
        {
            const VkCommandPoolCreateInfo poolInfo{
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                .queueFamilyIndex = graphicsFamily,
            };
            VkCommandPool pool = VK_NULL_HANDLE;
            if (!Check(vkCreateCommandPool(device_, &poolInfo, nullptr, &pool),
                       "vkCreateCommandPool", error))
                return false;
            slot.commandPool = pool;
            const VkCommandBufferAllocateInfo allocate{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            VkCommandBuffer buffer = VK_NULL_HANDLE;
            if (!Check(vkAllocateCommandBuffers(device_, &allocate, &buffer),
                       "vkAllocateCommandBuffers", error))
                return false;
            slot.commandBuffer = buffer;
            if (!CreateSemaphore(device_, slot.imageAvailable, error) ||
                !CreateFence(device_, VK_FENCE_CREATE_SIGNALED_BIT, slot.complete, error) ||
                !CreateFence(device_, 0, slot.acquired, error))
                return false;
        }
        error.clear();
        return true;
    }

    bool VulkanFrameResources::CreatePresentResources(const std::size_t imageCount,
                                                      const bool presentFences, std::string& error)
    {
        if (device_ == VK_NULL_HANDLE || imageCount == 0 || !images.empty())
        {
            error = "Present resources require a device, images, and an empty owner";
            return false;
        }
        images.resize(imageCount);
        for (auto& image : images)
        {
            if (!CreateSemaphore(device_, image.renderFinished, error) ||
                (presentFences && !CreateFence(device_, 0, image.released, error)))
            {
                ResetPresentResources();
                return false;
            }
        }
        error.clear();
        return true;
    }

    void VulkanFrameResources::ResetPresentResources() noexcept
    {
        for (const auto& image : images)
        {
            vkDestroyFence(device_, image.released, nullptr);
            vkDestroySemaphore(device_, image.renderFinished, nullptr);
        }
        images.clear();
    }
} // namespace owl::vulkan
