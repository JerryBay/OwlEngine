#include "VulkanImageUpload.h"

#include "VulkanAllocator.h"
#include "VulkanDevice.h"

#include <exception>
#include <utility>

namespace owl::vulkan::detail
{
    std::optional<std::size_t> ImageUploadByteSize(const VulkanImageDesc& desc) noexcept
    {
        if (!IsImageDescValid(desc) || desc.mipLevels != 1 ||
            (desc.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
            return std::nullopt;
        constexpr auto limit = std::numeric_limits<std::size_t>::max();
        const auto width = static_cast<std::size_t>(desc.extent.width);
        const auto height = static_cast<std::size_t>(desc.extent.height);
        if (width > limit / 4)
            return std::nullopt;
        const auto rowBytes = width * 4;
        if (height > limit / rowBytes)
            return std::nullopt;
        return rowBytes * height;
    }

    ImageUploadState StateAfterImageUploadSubmit(const ImageUploadState state,
                                                 const VkResult result) noexcept
    {
        if (state != ImageUploadState::NotSubmitted)
            return state;
        if (result == VK_SUCCESS)
            return ImageUploadState::Pending;
        if (result == VK_ERROR_DEVICE_LOST)
            return ImageUploadState::DeviceLost;
        return state;
    }

    ImageUploadState StateAfterImageUploadWait(const ImageUploadState state,
                                               const VkResult result) noexcept
    {
        if (state != ImageUploadState::Pending)
            return state;
        if (result == VK_SUCCESS)
            return ImageUploadState::Completed;
        if (result == VK_ERROR_DEVICE_LOST)
            return ImageUploadState::DeviceLost;
        return state;
    }
} // namespace owl::vulkan::detail

namespace owl::vulkan
{
    namespace
    {
        bool Check(const VkResult result, const char* operation, std::string& error)
        {
            if (result == VK_SUCCESS)
                return true;
            error = std::string{operation} + " failed with VkResult " +
                    std::to_string(static_cast<int>(result));
            return false;
        }
    } // namespace

    VulkanImageUpload::~VulkanImageUpload()
    {
        Reset();
    }

    VulkanImageUpload::VulkanImageUpload(VulkanImageUpload&& other) noexcept
        : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
          queue_(std::exchange(other.queue_, VK_NULL_HANDLE)),
          commandPool_(std::exchange(other.commandPool_, VK_NULL_HANDLE)),
          commandBuffer_(std::exchange(other.commandBuffer_, VK_NULL_HANDLE)),
          fence_(std::exchange(other.fence_, VK_NULL_HANDLE)),
          staging_(std::exchange(other.staging_, std::nullopt)),
          destination_(std::exchange(other.destination_, std::nullopt)),
          stagingMemoryProperties_(std::exchange(other.stagingMemoryProperties_, 0)),
          state_(std::exchange(other.state_, detail::ImageUploadState::NotSubmitted))
    {
    }

    VulkanImageUpload& VulkanImageUpload::operator=(VulkanImageUpload&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            queue_ = std::exchange(other.queue_, VK_NULL_HANDLE);
            commandPool_ = std::exchange(other.commandPool_, VK_NULL_HANDLE);
            commandBuffer_ = std::exchange(other.commandBuffer_, VK_NULL_HANDLE);
            fence_ = std::exchange(other.fence_, VK_NULL_HANDLE);
            staging_ = std::exchange(other.staging_, std::nullopt);
            destination_ = std::exchange(other.destination_, std::nullopt);
            stagingMemoryProperties_ = std::exchange(other.stagingMemoryProperties_, 0);
            state_ = std::exchange(other.state_, detail::ImageUploadState::NotSubmitted);
        }
        return *this;
    }

    std::optional<VulkanImageUpload> VulkanImageUpload::Create(
        const VulkanDevice& device, const VulkanAllocator& allocator, const VulkanImageDesc& desc,
        const std::span<const std::byte> bytes, std::string& error)
    {
        if (!device.IsValid() || device.GraphicsQueue() == VK_NULL_HANDLE ||
            !device.QueueFamilies().graphicsFamily)
        {
            error = "Image upload requires a valid device and graphics queue";
            return std::nullopt;
        }
        if (!allocator.IsValid() || allocator.Device() != device.Get())
        {
            error = "Image upload requires an allocator belonging to its device";
            return std::nullopt;
        }
        const auto byteSize = detail::ImageUploadByteSize(desc);
        if (!byteSize || bytes.size() != *byteSize)
        {
            error = "Image upload requires a single-mip sampled RGBA8 transfer destination "
                    "and exactly width * height * 4 bytes";
            return std::nullopt;
        }

        VulkanImageUpload result;
        result.device_ = device.Get();
        result.queue_ = device.GraphicsQueue();
        // Query format/extent support before allocating the staging payload.
        result.destination_ = VulkanImage::Create(allocator, desc, error);
        if (!result.destination_)
            return std::nullopt;
        const VulkanBufferDesc stagingDesc{
            .size = *byteSize,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            .preferredMemory = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            .hostAccess = BufferHostAccess::SequentialWrite,
        };
        result.staging_ = VulkanBuffer::Create(allocator, stagingDesc, error);
        if (!result.staging_ || !result.staging_->Write(0, bytes, error))
            return std::nullopt;
        result.stagingMemoryProperties_ = result.staging_->MemoryProperties();

        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = *device.QueueFamilies().graphicsFamily,
        };
        // Failed creation outputs are not owned, even if the implementation wrote to them.
        VkCommandPool pool = VK_NULL_HANDLE;
        if (!Check(vkCreateCommandPool(result.device_, &poolInfo, nullptr, &pool),
                   "vkCreateCommandPool(image upload)", error))
            return std::nullopt;
        result.commandPool_ = pool;
        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer command = VK_NULL_HANDLE;
        if (!Check(vkAllocateCommandBuffers(result.device_, &allocateInfo, &command),
                   "vkAllocateCommandBuffers(image upload)", error))
            return std::nullopt;
        result.commandBuffer_ = command;
        const VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        if (!Check(vkBeginCommandBuffer(command, &beginInfo),
                   "vkBeginCommandBuffer(image upload)", error))
            return std::nullopt;

        const VkImageSubresourceRange range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        // This owner always creates a new image, so there is no previous image access to wait for.
        // Host writes are flushed by staging.Write before submission's host-to-device operation.
        const VkImageMemoryBarrier2 toCopy{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = result.destination_->Get(),
            .subresourceRange = range,
        };
        const VkDependencyInfo beforeCopy{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toCopy,
        };
        vkCmdPipelineBarrier2(command, &beforeCopy);
        const VkBufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {desc.extent.width, desc.extent.height, 1},
        };
        vkCmdCopyBufferToImage(command, result.staging_->Get(), result.destination_->Get(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        const VkImageMemoryBarrier2 toSampling{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = result.destination_->Get(),
            .subresourceRange = range,
        };
        const VkDependencyInfo afterCopy{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toSampling,
        };
        vkCmdPipelineBarrier2(command, &afterCopy);
        if (!Check(vkEndCommandBuffer(command), "vkEndCommandBuffer(image upload)", error))
            return std::nullopt;
        const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        if (!Check(vkCreateFence(result.device_, &fenceInfo, nullptr, &fence),
                   "vkCreateFence(image upload)", error))
            return std::nullopt;
        result.fence_ = fence;
        error.clear();
        return result;
    }

    VkResult VulkanImageUpload::Submit(std::string& error)
    {
        if (state_ == detail::ImageUploadState::DeviceLost)
        {
            error = "Image upload device is lost";
            return VK_ERROR_DEVICE_LOST;
        }
        if (state_ != detail::ImageUploadState::NotSubmitted || device_ == VK_NULL_HANDLE ||
            queue_ == VK_NULL_HANDLE || commandBuffer_ == VK_NULL_HANDLE || fence_ == VK_NULL_HANDLE)
        {
            error = "Image upload was already submitted or is invalid";
            return VK_NOT_READY;
        }
        const VkCommandBufferSubmitInfo commandInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commandBuffer_,
        };
        const VkSubmitInfo2 submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandInfo,
        };
        const VkResult submitted = vkQueueSubmit2(queue_, 1, &submit, fence_);
        // Record ownership state before building a potentially allocating error message.
        state_ = detail::StateAfterImageUploadSubmit(state_, submitted);
        if (Check(submitted, "vkQueueSubmit2(image upload)", error))
            error.clear();
        return submitted;
    }

    VkResult VulkanImageUpload::Wait(std::string& error, const std::uint64_t timeoutNanoseconds)
    {
        if (state_ == detail::ImageUploadState::Completed)
        {
            error.clear();
            return VK_SUCCESS;
        }
        if (state_ == detail::ImageUploadState::DeviceLost)
        {
            error = "Image upload device is lost";
            return VK_ERROR_DEVICE_LOST;
        }
        if (!IsPending())
        {
            error = "Image upload has not been submitted";
            return VK_NOT_READY;
        }
        const VkResult waited = vkWaitForFences(device_, 1, &fence_, VK_TRUE, timeoutNanoseconds);
        ObserveWaitResult(waited);
        if (Check(waited, "vkWaitForFences(image upload)", error))
            error.clear();
        return waited;
    }

    bool VulkanImageUpload::IsPending() const noexcept
    {
        return state_ == detail::ImageUploadState::Pending;
    }

    VkResult VulkanImageUpload::DrainForDestruction() noexcept
    {
        if (state_ == detail::ImageUploadState::DeviceLost)
            return VK_ERROR_DEVICE_LOST;
        if (!IsPending())
            return VK_SUCCESS;
        VkResult result = vkWaitForFences(
            device_, 1, &fence_, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
            result = vkQueueWaitIdle(queue_);
        ObserveWaitResult(result);
        return result;
    }

    VkMemoryPropertyFlags VulkanImageUpload::StagingMemoryProperties() const noexcept
    {
        return stagingMemoryProperties_;
    }

    std::optional<VulkanImage> VulkanImageUpload::TakeDestination(std::string& error)
    {
        if (state_ != detail::ImageUploadState::Completed || !destination_)
        {
            error = "Image upload destination is not ready or was already transferred";
            return std::nullopt;
        }
        auto result = std::exchange(destination_, std::nullopt);
        error.clear();
        return result;
    }

    void VulkanImageUpload::ObserveWaitResult(const VkResult result) noexcept
    {
        state_ = detail::StateAfterImageUploadWait(state_, result);
        if (state_ == detail::ImageUploadState::Completed)
        {
            ReleaseTransient();
            staging_.reset();
        }
    }

    void VulkanImageUpload::ReleaseTransient() noexcept
    {
        if (fence_ != VK_NULL_HANDLE)
            vkDestroyFence(device_, fence_, nullptr);
        if (commandPool_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        fence_ = VK_NULL_HANDLE;
        commandPool_ = VK_NULL_HANDLE;
        commandBuffer_ = VK_NULL_HANDLE;
    }

    void VulkanImageUpload::Reset() noexcept
    {
        if (IsPending())
            std::terminate();
        ReleaseTransient();
        staging_.reset();
        destination_.reset();
        device_ = VK_NULL_HANDLE;
        queue_ = VK_NULL_HANDLE;
        stagingMemoryProperties_ = 0;
        state_ = detail::ImageUploadState::NotSubmitted;
    }
} // namespace owl::vulkan
