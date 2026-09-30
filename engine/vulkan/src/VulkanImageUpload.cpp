#include "VulkanImageUpload.h"

#include "VulkanAllocator.h"
#include "VulkanDevice.h"
#include "VulkanMipChain.h"

#include <exception>
#include <algorithm>
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

    MipUploadPath ChooseMipUploadPath(const VulkanImageDesc& desc,
                                     const VkFormatFeatureFlags optimalFeatures,
                                     const MipGenerationMode mode) noexcept
    {
        if (desc.mipLevels == 1)
            return MipUploadPath::SingleLevel;
        constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                                  VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if (mode == MipGenerationMode::Auto &&
            (desc.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0 &&
            desc.extent.width <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) &&
            desc.extent.height <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) &&
            (optimalFeatures & required) == required)
            return MipUploadPath::GpuBlit;
        return MipUploadPath::CpuUpload;
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
          path_(std::exchange(other.path_, MipUploadPath::SingleLevel)),
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
            path_ = std::exchange(other.path_, MipUploadPath::SingleLevel);
            state_ = std::exchange(other.state_, detail::ImageUploadState::NotSubmitted);
        }
        return *this;
    }

    std::optional<VulkanImageUpload> VulkanImageUpload::Create(
        const VulkanDevice& device, const VulkanAllocator& allocator, const VulkanImageDesc& desc,
        const std::span<const std::byte> bytes, std::string& error,
        const MipGenerationMode mode)
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
        auto baseDesc = desc;
        baseDesc.mipLevels = 1;
        const auto byteSize = detail::ImageUploadByteSize(baseDesc);
        if (!detail::IsImageDescValid(desc) || !byteSize || bytes.size() != *byteSize)
        {
            error = "Image upload requires a sampled RGBA8 transfer destination "
                    "and exactly width * height * 4 bytes";
            return std::nullopt;
        }

        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device.PhysicalDevice(), desc.format, &properties);
        const auto path = detail::ChooseMipUploadPath(desc, properties.optimalTilingFeatures, mode);
        std::optional<detail::CpuMipChain> chain;
        std::span<const std::byte> stagingBytes = bytes;
        if (path == MipUploadPath::CpuUpload)
        {
            chain = detail::BuildCpuMipChain(desc, bytes, error);
            if (!chain)
                return std::nullopt;
            stagingBytes = chain->bytes;
        }

        VulkanImageUpload result;
        result.device_ = device.Get();
        result.queue_ = device.GraphicsQueue();
        result.path_ = path;
        // Query format/extent support before allocating the staging payload.
        result.destination_ = VulkanImage::Create(allocator, desc, error);
        if (!result.destination_)
            return std::nullopt;
        const VulkanBufferDesc stagingDesc{
            .size = stagingBytes.size(),
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            .preferredMemory = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            .hostAccess = BufferHostAccess::SequentialWrite,
        };
        result.staging_ = VulkanBuffer::Create(allocator, stagingDesc, error);
        if (!result.staging_ || !result.staging_->Write(0, stagingBytes, error))
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

        const auto transition = [&](const std::uint32_t level, const std::uint32_t count,
                                    const VkImageLayout oldLayout, const VkImageLayout newLayout,
                                    const VkPipelineStageFlags2 srcStage, const VkAccessFlags2 srcAccess,
                                    const VkPipelineStageFlags2 dstStage, const VkAccessFlags2 dstAccess)
        {
            const VkImageMemoryBarrier2 barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = srcStage,
                .srcAccessMask = srcAccess,
                .dstStageMask = dstStage,
                .dstAccessMask = dstAccess,
                .oldLayout = oldLayout,
                .newLayout = newLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = result.destination_->Get(),
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, count, 0, 1},
            };
            const VkDependencyInfo dependency{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier,
            };
            vkCmdPipelineBarrier2(command, &dependency);
        };
        const auto copy = [&](const std::uint32_t level, const VkExtent2D extent,
                              const VkDeviceSize offset)
        {
            const VkBufferImageCopy region{
                .bufferOffset = offset,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
                .imageOffset = {0, 0, 0},
                .imageExtent = {extent.width, extent.height, 1},
            };
            vkCmdCopyBufferToImage(command, result.staging_->Get(), result.destination_->Get(),
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        };
        transition(0, path == MipUploadPath::CpuUpload ? desc.mipLevels : 1,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                   VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT);
        copy(0, desc.extent, 0);
        if (path == MipUploadPath::CpuUpload)
        {
            for (std::uint32_t level = 1; level < desc.mipLevels; ++level)
                copy(level, chain->levels[level].extent, chain->levels[level].offset);
            transition(0, desc.mipLevels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
        else if (path == MipUploadPath::GpuBlit)
        {
            VkExtent2D source = desc.extent;
            for (std::uint32_t level = 1; level < desc.mipLevels; ++level)
            {
                const VkExtent2D target{
                    std::max(1U, source.width / 2), std::max(1U, source.height / 2)};
                transition(level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           level == 1 ? VK_PIPELINE_STAGE_2_COPY_BIT : VK_PIPELINE_STAGE_2_BLIT_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
                           VK_ACCESS_2_TRANSFER_READ_BIT);
                transition(level, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                           VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
                const VkImageBlit region{
                    .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1},
                    .srcOffsets = {{0, 0, 0}, {static_cast<std::int32_t>(source.width),
                                              static_cast<std::int32_t>(source.height), 1}},
                    .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
                    .dstOffsets = {{0, 0, 0}, {static_cast<std::int32_t>(target.width),
                                              static_cast<std::int32_t>(target.height), 1}},
                };
                vkCmdBlitImage(command, result.destination_->Get(),
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, result.destination_->Get(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);
                transition(level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                source = target;
            }
            transition(desc.mipLevels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
        else
        {
            transition(0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
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

    void VulkanImageUpload::MarkCompleteAfterQueueIdleForDestruction() noexcept
    {
        if (IsPending())
            ObserveWaitResult(VK_SUCCESS);
    }

    void VulkanImageUpload::MarkDeviceLostForDestruction() noexcept
    {
        if (IsPending())
            state_ = detail::ImageUploadState::DeviceLost;
    }

    VkMemoryPropertyFlags VulkanImageUpload::StagingMemoryProperties() const noexcept
    {
        return stagingMemoryProperties_;
    }

    MipUploadPath VulkanImageUpload::Path() const noexcept { return path_; }

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
        path_ = MipUploadPath::SingleLevel;
        state_ = detail::ImageUploadState::NotSubmitted;
    }
} // namespace owl::vulkan
