#include "VulkanBufferUpload.h"

#include "VulkanDevice.h"

#include <limits>
#include <exception>
#include <string>
#include <utility>

namespace owl::vulkan::detail
{
    BufferUploadValidation ValidateBufferUploadArguments(
        const bool deviceValid, const bool graphicsQueueValid, const std::size_t payloadSize,
        const VkBufferUsageFlags finalUsage, const VkPipelineStageFlags2 consumerStages,
        const VkAccessFlags2 consumerAccess) noexcept
    {
        constexpr VkBufferUsageFlags allowed = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (!deviceValid)
            return BufferUploadValidation::InvalidDevice;
        if (payloadSize == 0)
            return BufferUploadValidation::EmptyPayload;
        if (finalUsage == 0 || (finalUsage & ~allowed) != 0)
            return BufferUploadValidation::InvalidUsage;
        if (consumerStages == 0)
            return BufferUploadValidation::MissingConsumerStage;
        if (consumerAccess == 0)
            return BufferUploadValidation::MissingConsumerAccess;
        if (!graphicsQueueValid)
            return BufferUploadValidation::MissingGraphicsQueue;
        return BufferUploadValidation::Valid;
    }

    BufferUploadState StateAfterSubmit(const BufferUploadState state,
                                       const VkResult result) noexcept
    {
        if (state != BufferUploadState::NotSubmitted)
            return state;
        if (result == VK_SUCCESS)
            return BufferUploadState::Pending;
        if (result == VK_ERROR_DEVICE_LOST)
            return BufferUploadState::DeviceLost;
        return BufferUploadState::NotSubmitted;
    }

    BufferUploadState StateAfterWait(const BufferUploadState state,
                                     const VkResult result) noexcept
    {
        if (state != BufferUploadState::Pending)
            return state;
        if (result == VK_SUCCESS)
            return BufferUploadState::Completed;
        if (result == VK_ERROR_DEVICE_LOST)
            return BufferUploadState::DeviceLost;
        return BufferUploadState::Pending;
    }

    bool IsBufferUploadPending(const BufferUploadState state) noexcept
    {
        return state == BufferUploadState::Pending;
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

    VulkanBufferUpload::~VulkanBufferUpload()
    {
        if (state_ == detail::BufferUploadState::Pending)
            std::terminate();
        Reset();
    }

    VulkanBufferUpload::VulkanBufferUpload(VulkanBufferUpload&& other) noexcept
        : device_(std::exchange(other.device_, nullptr)),
          commandPool_(std::exchange(other.commandPool_, VK_NULL_HANDLE)),
          commandBuffer_(std::exchange(other.commandBuffer_, VK_NULL_HANDLE)),
          fence_(std::exchange(other.fence_, VK_NULL_HANDLE)),
          staging_(std::move(other.staging_)),
          destination_(std::move(other.destination_)),
          stagingMemoryProperties_(std::exchange(other.stagingMemoryProperties_, 0)),
          state_(std::exchange(other.state_, detail::BufferUploadState::NotSubmitted))
    {
    }

    VulkanBufferUpload& VulkanBufferUpload::operator=(VulkanBufferUpload&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            device_ = std::exchange(other.device_, nullptr);
            commandPool_ = std::exchange(other.commandPool_, VK_NULL_HANDLE);
            commandBuffer_ = std::exchange(other.commandBuffer_, VK_NULL_HANDLE);
            fence_ = std::exchange(other.fence_, VK_NULL_HANDLE);
            staging_ = std::move(other.staging_);
            destination_ = std::move(other.destination_);
            stagingMemoryProperties_ = std::exchange(other.stagingMemoryProperties_, 0);
            state_ = std::exchange(other.state_, detail::BufferUploadState::NotSubmitted);
        }
        return *this;
    }

    std::optional<VulkanBufferUpload>
    VulkanBufferUpload::Create(const VulkanDevice& device, const std::span<const std::byte> bytes,
                               const VkBufferUsageFlags finalUsage,
                               const VkPipelineStageFlags2 consumerStages,
                               const VkAccessFlags2 consumerAccess, std::string& error)
    {
        const auto validation = detail::ValidateBufferUploadArguments(
            device.IsValid(),
            device.GraphicsQueue() != VK_NULL_HANDLE && device.QueueFamilies().graphicsFamily.has_value(),
            bytes.size(), finalUsage, consumerStages, consumerAccess);
        if (validation != detail::BufferUploadValidation::Valid)
        {
            switch (validation)
            {
            case detail::BufferUploadValidation::InvalidDevice:
                error = "Buffer upload requires a valid device";
                break;
            case detail::BufferUploadValidation::EmptyPayload:
                error = "Buffer upload requires a non-empty payload";
                break;
            case detail::BufferUploadValidation::InvalidUsage:
                error = "Buffer upload has invalid final usage flags";
                break;
            case detail::BufferUploadValidation::MissingConsumerStage:
                error = "Buffer upload requires a consumer stage mask";
                break;
            case detail::BufferUploadValidation::MissingConsumerAccess:
                error = "Buffer upload requires a consumer access mask";
                break;
            case detail::BufferUploadValidation::MissingGraphicsQueue:
                error = "Buffer upload requires a graphics queue family";
                break;
            case detail::BufferUploadValidation::Valid:
                break;
            }
            return std::nullopt;
        }

        VulkanBufferUpload result;
        result.device_ = &device;
        const VulkanBufferDesc stagingDesc{
            .size = bytes.size(),
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .requiredMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            .preferredMemory = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        };
        result.staging_ = VulkanBuffer::Create(device, stagingDesc, error);
        if (!result.staging_)
            return std::nullopt;
        result.stagingMemoryProperties_ = result.staging_->MemoryProperties();

        const VulkanBufferDesc destinationDesc{
            .size = bytes.size(),
            .usage = finalUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .requiredMemory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        };
        result.destination_ = VulkanBuffer::Create(device, destinationDesc, error);
        if (!result.destination_)
            return std::nullopt;
        if (!result.staging_->Write(0, bytes, error))
            return std::nullopt;

        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = *device.QueueFamilies().graphicsFamily,
        };
        if (!Check(vkCreateCommandPool(device.Get(), &poolInfo, nullptr, &result.commandPool_),
                   "vkCreateCommandPool", error))
            return std::nullopt;

        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = result.commandPool_,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (!Check(vkAllocateCommandBuffers(device.Get(), &allocateInfo, &result.commandBuffer_),
                   "vkAllocateCommandBuffers", error))
            return std::nullopt;

        const VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        if (!Check(vkBeginCommandBuffer(result.commandBuffer_, &beginInfo), "vkBeginCommandBuffer",
                   error))
            return std::nullopt;

        const VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = result.destination_->Size()};
        vkCmdCopyBuffer(result.commandBuffer_, result.staging_->Get(), result.destination_->Get(),
                        1, &region);
        const VkBufferMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = consumerStages,
            .dstAccessMask = consumerAccess,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = result.destination_->Get(),
            .offset = 0,
            .size = result.destination_->Size(),
        };
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(result.commandBuffer_, &dependency);
        if (!Check(vkEndCommandBuffer(result.commandBuffer_), "vkEndCommandBuffer", error))
            return std::nullopt;

        const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (!Check(vkCreateFence(device.Get(), &fenceInfo, nullptr, &result.fence_), "vkCreateFence",
                   error))
            return std::nullopt;

        error.clear();
        return result;
    }

    VkResult VulkanBufferUpload::SubmitAndWait(std::string& error)
    {
        if (state_ == detail::BufferUploadState::DeviceLost)
        {
            error = "Buffer upload device is lost";
            return VK_ERROR_DEVICE_LOST;
        }
        if (state_ != detail::BufferUploadState::NotSubmitted || device_ == nullptr ||
            commandBuffer_ == VK_NULL_HANDLE || fence_ == VK_NULL_HANDLE)
        {
            error = "Buffer upload was already submitted or is invalid";
            return VK_NOT_READY;
        }

        const VkCommandBufferSubmitInfo command{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commandBuffer_,
        };
        const VkSubmitInfo2 submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &command,
        };
        const VkResult submitResult = vkQueueSubmit2(device_->GraphicsQueue(), 1, &submit, fence_);
        state_ = detail::StateAfterSubmit(state_, submitResult);
        if (submitResult != VK_SUCCESS)
        {
            Check(submitResult, "vkQueueSubmit2", error);
            return submitResult;
        }
        return Wait(error);
    }

    VkResult VulkanBufferUpload::Wait(std::string& error)
    {
        if (state_ == detail::BufferUploadState::Completed)
        {
            error.clear();
            return VK_SUCCESS;
        }
        if (state_ == detail::BufferUploadState::DeviceLost)
        {
            error = "Buffer upload device is lost";
            return VK_ERROR_DEVICE_LOST;
        }
        if (state_ != detail::BufferUploadState::Pending || device_ == nullptr ||
            fence_ == VK_NULL_HANDLE)
        {
            error = "Buffer upload has not been submitted";
            return VK_NOT_READY;
        }

        const VkResult waitResult =
            vkWaitForFences(device_->Get(), 1, &fence_, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
        state_ = detail::StateAfterWait(state_, waitResult);
        if (waitResult != VK_SUCCESS)
        {
            Check(waitResult, "vkWaitForFences", error);
            return waitResult;
        }

        staging_.reset();
        ReleaseTransient();
        error.clear();
        return VK_SUCCESS;
    }

    bool VulkanBufferUpload::IsPending() const noexcept
    {
        return detail::IsBufferUploadPending(state_);
    }

    VkResult VulkanBufferUpload::DrainForDestruction() noexcept
    {
        if (state_ == detail::BufferUploadState::DeviceLost)
            return VK_ERROR_DEVICE_LOST;
        if (state_ != detail::BufferUploadState::Pending)
            return VK_SUCCESS;
        if (device_ == nullptr || !device_->IsValid() || fence_ == VK_NULL_HANDLE)
            return VK_ERROR_UNKNOWN;

        const VkResult result = vkWaitForFences(
            device_->Get(), 1, &fence_, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
        state_ = detail::StateAfterWait(state_, result);
        if (result == VK_SUCCESS)
        {
            staging_.reset();
            ReleaseTransient();
        }
        return result;
    }

    void VulkanBufferUpload::MarkDeviceLostForDestruction() noexcept
    {
        if (state_ == detail::BufferUploadState::Pending)
            state_ = detail::BufferUploadState::DeviceLost;
    }

    void VulkanBufferUpload::MarkCompleteAfterQueueIdleForDestruction() noexcept
    {
        if (state_ != detail::BufferUploadState::Pending)
            return;
        state_ = detail::BufferUploadState::Completed;
        staging_.reset();
        ReleaseTransient();
    }

    VkMemoryPropertyFlags VulkanBufferUpload::StagingMemoryProperties() const noexcept
    {
        return stagingMemoryProperties_;
    }

    std::optional<VulkanBuffer> VulkanBufferUpload::TakeDestination(std::string& error)
    {
        if (state_ != detail::BufferUploadState::Completed || !destination_)
        {
            error = "Buffer upload destination is not ready";
            return std::nullopt;
        }
        auto result = std::exchange(destination_, std::nullopt);
        error.clear();
        return result;
    }

    void VulkanBufferUpload::ReleaseTransient() noexcept
    {
        if (device_ == nullptr || !device_->IsValid())
        {
            commandPool_ = VK_NULL_HANDLE;
            commandBuffer_ = VK_NULL_HANDLE;
            fence_ = VK_NULL_HANDLE;
            return;
        }
        if (fence_ != VK_NULL_HANDLE)
            vkDestroyFence(device_->Get(), fence_, nullptr);
        if (commandPool_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_->Get(), commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
        commandBuffer_ = VK_NULL_HANDLE;
        fence_ = VK_NULL_HANDLE;
    }

    void VulkanBufferUpload::Reset() noexcept
    {
        if (state_ == detail::BufferUploadState::Pending)
            std::terminate();
        ReleaseTransient();
        staging_.reset();
        destination_.reset();
        device_ = nullptr;
        stagingMemoryProperties_ = 0;
        state_ = detail::BufferUploadState::NotSubmitted;
    }
} // namespace owl::vulkan
