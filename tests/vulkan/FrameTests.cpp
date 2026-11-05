#include <catch2/catch_test_macros.hpp>

#include "SDLWindowAccess.h"
#include "VulkanFrame.h"
#include <SDL3/SDL.h>
#include <owl/foundation/Log.h>
#include <owl/platform/Platform.h>
#include <owl/vulkan/VulkanTriangle.h>

#include <array>
#include <chrono>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

TEST_CASE("Acquire results distinguish owned images from retry paths", "[vulkan][frame]")
{
    using owl::vulkan::detail::AcquireAction;
    const std::array cases{
        std::pair{VK_SUCCESS, AcquireAction::Render},
        std::pair{VK_SUBOPTIMAL_KHR, AcquireAction::Render},
        std::pair{VK_ERROR_OUT_OF_DATE_KHR, AcquireAction::Recreate},
        std::pair{VK_TIMEOUT, AcquireAction::Defer},
        std::pair{VK_NOT_READY, AcquireAction::Defer},
        std::pair{VK_ERROR_SURFACE_LOST_KHR, AcquireAction::Fail},
        std::pair{VK_ERROR_DEVICE_LOST, AcquireAction::Fail},
    };
    for (const auto& [result, expected] : cases)
    {
        CAPTURE(result);
        CHECK(owl::vulkan::detail::ClassifyAcquireResult(result) == expected);
    }
}

TEST_CASE("Present results track enqueued fences even for rejected surfaces", "[vulkan][frame]")
{
    using owl::vulkan::detail::PresentDecision;
    const std::array cases{
        std::pair{VK_SUCCESS, PresentDecision{true, false, false}},
        std::pair{VK_SUBOPTIMAL_KHR, PresentDecision{true, true, false}},
        std::pair{VK_ERROR_OUT_OF_DATE_KHR, PresentDecision{true, true, false}},
        std::pair{VK_ERROR_SURFACE_LOST_KHR, PresentDecision{true, false, true}},
        std::pair{VK_ERROR_OUT_OF_HOST_MEMORY, PresentDecision{false, false, true}},
        std::pair{VK_ERROR_OUT_OF_DEVICE_MEMORY, PresentDecision{false, false, true}},
        std::pair{VK_ERROR_DEVICE_LOST, PresentDecision{false, false, true}},
    };
    for (const auto& [result, expected] : cases)
    {
        CAPTURE(result);
        const auto actual = owl::vulkan::detail::ClassifyPresentResult(result);
        CHECK(actual.enqueued == expected.enqueued);
        CHECK(actual.recreate == expected.recreate);
        CHECK(actual.failed == expected.failed);
    }
}

TEST_CASE("Submission progress counts only successful graphics submits", "[vulkan][submission]")
{
    owl::vulkan::VulkanSubmissionProgress progress;
    owl::vulkan::VulkanFrameSlot first;
    owl::vulkan::VulkanFrameSlot second;
    CHECK(progress.CanSubmit(first));
    for (const auto result :
         {VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_DEVICE_LOST})
    {
        CAPTURE(result);
        progress.RecordSubmitResult(first, result);
        CHECK(progress.SubmittedSerial() == 0);
        CHECK(progress.CompletedSerial() == 0);
        CHECK(first.submissionSerial == 0);
        CHECK_FALSE(first.submissionPending);
    }
    progress.RecordSubmitResult(first, VK_SUCCESS);
    CHECK(first.submissionSerial == 1);
    CHECK(first.submissionPending);
    CHECK_FALSE(progress.CanSubmit(first));
    CHECK(progress.CanSubmit(second));
    progress.RecordSubmitResult(second, VK_SUCCESS);
    CHECK(second.submissionSerial == 2);
    CHECK(progress.SubmittedSerial() == 2);
    CHECK(progress.CompletedSerial() == 0);
    // A present/acquire result is not a successful queue submission.
    owl::vulkan::VulkanFrameSlot unused;
    progress.RecordSubmitResult(unused, VK_SUBOPTIMAL_KHR);
    CHECK(unused.submissionSerial == 0);
    CHECK(progress.SubmittedSerial() == 2);
}

TEST_CASE("Unsuccessful fence observations retain pending submission identity",
          "[vulkan][submission]")
{
    owl::vulkan::VulkanSubmissionProgress progress;
    owl::vulkan::VulkanFrameSlot slot;
    progress.RecordSubmitResult(slot, VK_SUCCESS);
    for (const auto result :
         {VK_TIMEOUT, VK_NOT_READY, VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_DEVICE_LOST})
    {
        CAPTURE(result);
        progress.RecordWaitResult(slot, result);
        CHECK(slot.submissionPending);
        CHECK(slot.submissionSerial == 1);
        CHECK(progress.SubmittedSerial() == 1);
        CHECK(progress.CompletedSerial() == 0);
        CHECK_FALSE(progress.CanSubmit(slot));
    }
    progress.RecordWaitResult(slot, VK_SUCCESS);
    CHECK_FALSE(slot.submissionPending);
    CHECK(progress.CanSubmit(slot));
    CHECK(progress.CompletedSerial() == 1);
}

TEST_CASE("Fence reuse preserves monotonically increasing submission identities",
          "[vulkan][submission]")
{
    owl::vulkan::VulkanSubmissionProgress progress;
    owl::vulkan::VulkanFrameSlot first;
    owl::vulkan::VulkanFrameSlot second;
    // An initial signaled fence without a submission is not graphics progress.
    progress.RecordWaitResult(first, VK_SUCCESS);
    CHECK(progress.CompletedSerial() == 0);
    progress.RecordSubmitResult(first, VK_SUCCESS);
    progress.RecordSubmitResult(second, VK_SUCCESS);
    progress.RecordWaitResult(second, VK_SUCCESS);
    CHECK(progress.CompletedSerial() == 2);
    progress.RecordWaitResult(first, VK_SUCCESS);
    CHECK(progress.CompletedSerial() == 2);
    CHECK_FALSE(first.submissionPending);
    progress.RecordSubmitResult(first, VK_SUCCESS);
    CHECK(first.submissionSerial == 3);
    CHECK(first.submissionPending);
    CHECK(progress.SubmittedSerial() == 3);
    CHECK(progress.CompletedSerial() == 2);
    // Re-observing the completed second slot cannot retire the new first-slot work.
    progress.RecordWaitResult(second, VK_SUCCESS);
    CHECK(progress.CompletedSerial() == 2);
    CHECK(first.submissionPending);
    progress.RecordWaitResult(first, VK_SUCCESS);
    CHECK(progress.CompletedSerial() == 3);
}

TEST_CASE("Submission serial exhaustion cannot wrap to the unsubmitted sentinel",
          "[vulkan][submission]")
{
    using owl::vulkan::detail::NextSubmissionSerial;
    CHECK(NextSubmissionSerial(0) == 1);
    CHECK(NextSubmissionSerial(41) == 42);
    CHECK(NextSubmissionSerial(std::numeric_limits<std::uint64_t>::max() - 1) ==
          std::numeric_limits<std::uint64_t>::max());
    CHECK_FALSE(NextSubmissionSerial(std::numeric_limits<std::uint64_t>::max()));
}

TEST_CASE("Frame upload byte accounting rejects overflow before submission",
          "[vulkan][frame-upload]")
{
    using owl::vulkan::detail::NextFrameUploadedBytes;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    CHECK(NextFrameUploadedBytes(0, 76) == 76);
    CHECK(NextFrameUploadedBytes(76, 76) == 152);
    CHECK(NextFrameUploadedBytes(maximum - 76, 76) == maximum);
    CHECK_FALSE(NextFrameUploadedBytes(maximum - 75, 76));
    CHECK(NextFrameUploadedBytes(maximum, 0) == maximum);
}

TEST_CASE("A failed submit preserves a completed slot's previous serial", "[vulkan][submission]")
{
    owl::vulkan::VulkanSubmissionProgress progress;
    owl::vulkan::VulkanFrameSlot slot;
    progress.RecordSubmitResult(slot, VK_SUCCESS);
    progress.RecordWaitResult(slot, VK_SUCCESS);
    progress.RecordSubmitResult(slot, VK_ERROR_OUT_OF_DEVICE_MEMORY);
    CHECK_FALSE(slot.submissionPending);
    CHECK(slot.submissionSerial == 1);
    CHECK(progress.SubmittedSerial() == 1);
    CHECK(progress.CompletedSerial() == 1);
    CHECK(progress.CanSubmit(slot));
    progress.RecordSubmitResult(slot, VK_SUCCESS);
    CHECK(slot.submissionSerial == 2);
    CHECK(progress.SubmittedSerial() == 2);
    CHECK(progress.CompletedSerial() == 1);
}

static_assert(!std::is_copy_constructible_v<owl::vulkan::VulkanTriangle>);
static_assert(std::is_nothrow_move_constructible_v<owl::vulkan::VulkanTriangle>);

TEST_CASE("Clear renderer rejects invalid windows and use after move", "[vulkan][frame]")
{
    std::string error;
    owl::platform::Window window;
    CHECK_FALSE(owl::vulkan::VulkanTriangle::Create(window, error));
    CHECK(error.find("window") != std::string::npos);
    owl::vulkan::VulkanTriangle empty;
    owl::vulkan::VulkanTriangle moved{std::move(empty)};
    CHECK(empty.RenderFrame(error) == owl::vulkan::FrameResult::Failed);
    CHECK_FALSE(error.empty());
    CHECK_FALSE(moved.IsValid());
    CHECK(moved.WaitIdle(error));
    CHECK(error.empty());
}

TEST_CASE("Renderer rejects simultaneous triangle and texture samples", "[vulkan][frame]")
{
    std::string error;
    owl::platform::Window window;
    owl::vulkan::VulkanTriangleOptions options;
    options.triangleShaders = owl::vulkan::TriangleShaderPaths{};
    options.textureShaders = owl::vulkan::TriangleShaderPaths{};
    CHECK_FALSE(owl::vulkan::VulkanTriangle::Create(window, error, options));
    CHECK(error.find("triangle and texture") != std::string::npos);
}

TEST_CASE("Geometry streaming rejects incompatible sample options before Vulkan initialization",
          "[vulkan][frame-upload]")
{
    std::string error;
    owl::platform::Window window;
    owl::vulkan::VulkanTriangleOptions options;
    options.streamTextureGeometry = true;
    options.textureMipMode = owl::vulkan::TextureMipMode::Generate;
    SECTION("clear renderer has no textured geometry") {}
    SECTION("triangle renderer has no mip texture shader interface")
    {
        options.triangleShaders = owl::vulkan::TriangleShaderPaths{};
    }
    SECTION("single-level texture shaders have no scale and LOD interface")
    {
        options.textureShaders = owl::vulkan::TriangleShaderPaths{};
        options.textureMipMode = owl::vulkan::TextureMipMode::SingleLevel;
    }
    CHECK_FALSE(owl::vulkan::VulkanTriangle::Create(window, error, options));
    CHECK(error.find("stream") != std::string::npos);
}

namespace
{
    enum class SampleMode
    {
        Clear,
        Triangle,
        Texture,
        TextureMips,
        TextureMipsCpu,
    };

    void RunLocalFrames(const SampleMode mode, const bool streamGeometry = false,
                        const bool replaceTextures = false)
    {
        const char* enabled = SDL_getenv("OWL_RUN_VULKAN_BOOTSTRAP_TEST");
        if (enabled == nullptr || std::string_view{enabled} != "1")
            SKIP("Set OWL_RUN_VULKAN_BOOTSTRAP_TEST=1 to exercise local clear presentation");

        struct LoggingScope
        {
            LoggingScope()
            {
                owl::foundation::InitializeLogging();
            }
            ~LoggingScope()
            {
                owl::foundation::ShutdownLogging();
            }
        } logging;
        bool usePresentFences = true;
        SECTION("automatic present-fence capability") {}
        SECTION("explicit compatibility fallback")
        {
            usePresentFences = false;
        }
        std::string error;
        auto platform = owl::platform::Platform::Create(error);
        INFO(error);
        REQUIRE(platform);
        auto window =
            platform->CreateWindow({.title = "OwlEngine - M1 Vulkan Clear",
                                    .width = 640,
                                    .height = 360,
                                    .resizable = true,
                                    .surfaceApi = owl::platform::WindowSurfaceApi::Vulkan},
                                   error);
        REQUIRE(window);
        owl::vulkan::VulkanTriangleOptions options{.enablePresentFences = usePresentFences};
        if (mode == SampleMode::Triangle)
        {
            const std::filesystem::path assets{OWL_TRIANGLE_TEST_ASSET_DIR};
            options.triangleShaders = owl::vulkan::TriangleShaderPaths{
                assets / "triangle.vert.spv", assets / "triangle.frag.spv"};
        }
        if (mode == SampleMode::Texture || mode == SampleMode::TextureMips ||
            mode == SampleMode::TextureMipsCpu)
        {
            const std::filesystem::path assets{OWL_TEXTURE_TEST_ASSET_DIR};
            options.textureShaders = owl::vulkan::TriangleShaderPaths{assets / "texture.vert.spv",
                                                                      assets / "texture.frag.spv"};
            if (mode != SampleMode::Texture)
            {
                options.textureShaders = owl::vulkan::TriangleShaderPaths{
                    assets / "texture_mips.vert.spv", assets / "texture_mips.frag.spv"};
                options.textureMipMode = mode == SampleMode::TextureMips
                                             ? owl::vulkan::TextureMipMode::Generate
                                             : owl::vulkan::TextureMipMode::ForceCpu;
            }
        }
        options.streamTextureGeometry = streamGeometry;
        auto renderer = owl::vulkan::VulkanTriangle::Create(*window, error, options);
        INFO(error);
        REQUIRE(renderer);
        REQUIRE(renderer->IsValid());
        const auto memoryBaseline = renderer->QueryMemoryStats();
        if (mode == SampleMode::Clear)
        {
            CHECK_FALSE(memoryBaseline);
        }
        else
        {
            REQUIRE(memoryBaseline);
            CHECK(memoryBaseline->allocationCount > 0);
            CHECK(memoryBaseline->allocationBytes > 0);
            CHECK(memoryBaseline->blockBytes >= memoryBaseline->allocationBytes);
        }
        const auto checkDrainedMemory = [&]
        {
            const auto memory = renderer->QueryMemoryStats();
            REQUIRE(memory.has_value() == memoryBaseline.has_value());
            if (memory)
            {
                CHECK(memory->allocationCount == memoryBaseline->allocationCount);
                CHECK(memory->allocationBytes == memoryBaseline->allocationBytes);
                CHECK(memory->blockBytes >= memory->allocationBytes);
            }
        };
        CHECK(renderer->Stats().submittedSerial == 0);
        CHECK(renderer->Stats().completedSerial == 0);
        const std::uint64_t expectedUploadCapacity = streamGeometry ? 2 * 64 * 1024 : 0;
        REQUIRE(renderer->Stats().frameUploadCapacityBytes == expectedUploadCapacity);
        CHECK(renderer->Stats().frameUploadedBytes == 0);
        std::array<std::byte, 64 * 64 * 4> replacementPixels{};
        std::uint64_t acceptedReplacements = 0;
        const auto requestReplacement = [&]
        {
            replacementPixels.fill(static_cast<std::byte>(acceptedReplacements % 255));
            REQUIRE(renderer->RequestTextureReplacement(replacementPixels, error) ==
                    owl::vulkan::TextureReplacementResult::Accepted);
            ++acceptedReplacements;
            REQUIRE(renderer->Stats().textureReplacementPending);
            CHECK(renderer->RequestTextureReplacement(replacementPixels, error) ==
                  owl::vulkan::TextureReplacementResult::Busy);
            CHECK(error.empty());
            const auto progressBeforeQuery = renderer->Stats();
            const auto pendingMemory = renderer->QueryMemoryStats();
            REQUIRE(pendingMemory);
            // Request retains both the new image and staging, even if the GPU already finished.
            CHECK(pendingMemory->allocationCount >= memoryBaseline->allocationCount + 2);
            CHECK(pendingMemory->allocationBytes > memoryBaseline->allocationBytes);
            CHECK(pendingMemory->blockBytes >= pendingMemory->allocationBytes);
            const auto progressAfterQuery = renderer->Stats();
            CHECK(progressAfterQuery.submittedSerial == progressBeforeQuery.submittedSerial);
            CHECK(progressAfterQuery.completedSerial == progressBeforeQuery.completedSerial);
            CHECK(progressAfterQuery.textureReplacements ==
                  progressBeforeQuery.textureReplacements);
            CHECK(progressAfterQuery.pendingTextureRetirements ==
                  progressBeforeQuery.pendingTextureRetirements);
            CHECK(progressAfterQuery.textureReplacementPending);
        };
        if (replaceTextures)
        {
            CHECK(renderer->RequestTextureReplacement({}, error) ==
                  owl::vulkan::TextureReplacementResult::Failed);
            CHECK(renderer->IsValid());
        }
        else if (mode == SampleMode::Clear || mode == SampleMode::Triangle)
        {
            CHECK(renderer->RequestTextureReplacement(replacementPixels, error) ==
                  owl::vulkan::TextureReplacementResult::Failed);
            CHECK(renderer->IsValid());
        }
        const auto checkUploadStats = [&](const owl::vulkan::VulkanTriangleStats& stats)
        {
            CHECK(stats.frameUploadCapacityBytes == expectedUploadCapacity);
            // Four position/UV vertices (64 bytes) plus six uint16 indices (12 bytes).
            CHECK(stats.frameUploadedBytes == (streamGeometry ? 76 * stats.submittedFrames : 0));
        };
        const auto checkInvalidDraw = [&]
        {
            const auto before = renderer->Stats();
            for (const auto& parameters : {owl::vulkan::TextureDrawParameters{.scale = 0.0F},
                                           owl::vulkan::TextureDrawParameters{.lod = 7.0F}})
            {
                CHECK(renderer->RenderFrame(error, parameters) == owl::vulkan::FrameResult::Failed);
                CHECK_FALSE(error.empty());
                CHECK(renderer->IsValid());
                const auto after = renderer->Stats();
                CHECK(after.submittedSerial == before.submittedSerial);
                CHECK(after.completedSerial == before.completedSerial);
                CHECK(after.frameUploadCapacityBytes == before.frameUploadCapacityBytes);
                CHECK(after.frameUploadedBytes == before.frameUploadedBytes);
            }
        };
        if (mode == SampleMode::TextureMips || mode == SampleMode::TextureMipsCpu)
        {
            CHECK(renderer->Stats().textureMipLevels == 7);
            if (mode == SampleMode::TextureMipsCpu)
                CHECK_FALSE(renderer->Stats().textureGpuMipGeneration);
            checkInvalidDraw();
        }
        if (!usePresentFences)
            CHECK_FALSE(renderer->Stats().presentFencesEnabled);

        const auto renderFrames = [&](std::uint64_t count)
        {
            const auto target = renderer->Stats().presentedFrames + count;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
            while (renderer->Stats().presentedFrames < target &&
                   std::chrono::steady_clock::now() < deadline)
            {
                REQUIRE(platform->PumpEvents() == owl::platform::EventPumpResult::Continue);
                owl::vulkan::TextureDrawParameters parameters;
                if (mode == SampleMode::TextureMips || mode == SampleMode::TextureMipsCpu)
                {
                    const auto phase = renderer->Stats().presentedFrames % 8;
                    if (phase < 7)
                        parameters.lod = static_cast<float>(phase);
                    else
                        parameters.scale = 1.0F / 128;
                }
                const auto before = renderer->Stats();
                // Cover the original startup texture before exercising replacement ownership.
                if (replaceTextures && before.submittedFrames > 0 && acceptedReplacements < 32 &&
                    !before.textureReplacementPending)
                    requestReplacement();
                const auto result = renderer->RenderFrame(error, parameters);
                INFO(error);
                REQUIRE(result != owl::vulkan::FrameResult::Failed);
                const auto after = renderer->Stats();
                if (replaceTextures)
                {
                    CHECK(after.pendingTextureRetirements <= 4);
                    CHECK(after.textureReplacements <= acceptedReplacements);
                    if (after.submittedSerial > before.submittedSerial)
                        CHECK(after.textureLastUseSerial == after.submittedSerial);
                }
                CHECK(after.submittedSerial == after.submittedFrames);
                CHECK(after.submittedSerial >= before.submittedSerial);
                CHECK(after.completedSerial >= before.completedSerial);
                CHECK(after.completedSerial <= after.submittedSerial);
                CHECK(after.submittedSerial - after.completedSerial <= 2);
                // Present can be out of date after a successful submit. That Deferred
                // frame still owns its serial; only pre-submit deferral allocates none.
                CHECK(after.submittedSerial - before.submittedSerial <= 1);
                if (result == owl::vulkan::FrameResult::Presented)
                    CHECK(after.submittedSerial == before.submittedSerial + 1);
                checkUploadStats(after);
                CHECK(after.frameUploadedBytes - before.frameUploadedBytes ==
                      (streamGeometry ? 76 * (after.submittedFrames - before.submittedFrames) : 0));
                if (result == owl::vulkan::FrameResult::Deferred)
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            REQUIRE(renderer->Stats().presentedFrames == target);
            CHECK(error.empty());
        };
        renderFrames(120);
        if (replaceTextures)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
            while (acceptedReplacements < 32 && std::chrono::steady_clock::now() < deadline)
                renderFrames(2);
            REQUIRE(acceptedReplacements == 32);
            REQUIRE(renderer->WaitIdle(error));
            CHECK(renderer->Stats().textureReplacements == acceptedReplacements);
            checkDrainedMemory();
            requestReplacement(); // Preserve accepted work through renderer moves.
        }
        if (mode == SampleMode::TextureMips || mode == SampleMode::TextureMipsCpu)
            checkInvalidDraw();
        const auto initial = renderer->Stats();
        const auto memoryBeforeMove = renderer->QueryMemoryStats();
        REQUIRE(initial.indexedDraws == (mode != SampleMode::Clear ? initial.submittedFrames : 0));
        owl::vulkan::VulkanTriangle moved{std::move(*renderer)};
        CHECK_FALSE(renderer->IsValid());
        CHECK_FALSE(renderer->QueryMemoryStats());
        const auto movedMemory = moved.QueryMemoryStats();
        REQUIRE(movedMemory.has_value() == memoryBeforeMove.has_value());
        if (movedMemory)
        {
            CHECK(movedMemory->allocationCount == memoryBeforeMove->allocationCount);
            CHECK(movedMemory->allocationBytes == memoryBeforeMove->allocationBytes);
            CHECK(movedMemory->blockCount == memoryBeforeMove->blockCount);
            CHECK(movedMemory->blockBytes == memoryBeforeMove->blockBytes);
        }
        CHECK(renderer->Stats().submittedSerial == 0);
        CHECK(renderer->Stats().completedSerial == 0);
        CHECK(renderer->Stats().frameUploadCapacityBytes == 0);
        CHECK(renderer->Stats().frameUploadedBytes == 0);
        CHECK(renderer->RenderFrame(error) == owl::vulkan::FrameResult::Failed);
        CHECK(moved.Stats().frameUploadCapacityBytes == initial.frameUploadCapacityBytes);
        CHECK(moved.Stats().frameUploadedBytes == initial.frameUploadedBytes);
        *renderer = std::move(moved);
        CHECK_FALSE(moved.QueryMemoryStats());
        const auto assignedMemory = renderer->QueryMemoryStats();
        REQUIRE(assignedMemory.has_value() == memoryBeforeMove.has_value());
        if (assignedMemory)
        {
            CHECK(assignedMemory->allocationCount == memoryBeforeMove->allocationCount);
            CHECK(assignedMemory->allocationBytes == memoryBeforeMove->allocationBytes);
            CHECK(assignedMemory->blockCount == memoryBeforeMove->blockCount);
            CHECK(assignedMemory->blockBytes == memoryBeforeMove->blockBytes);
        }
        if (replaceTextures)
            CHECK(renderer->Stats().textureReplacementPending);
        CHECK_FALSE(moved.IsValid());
        CHECK(moved.Stats().frameUploadCapacityBytes == 0);
        CHECK(moved.Stats().frameUploadedBytes == 0);
        CHECK(renderer->Stats().submittedSerial == initial.submittedSerial);
        CHECK(renderer->Stats().completedSerial == initial.completedSerial);
        checkUploadStats(renderer->Stats());

        SDL_Window* native = owl::platform::SDLWindowAccess::Get(*window);
        REQUIRE(native != nullptr);
        for (const auto [width, height] : {std::pair{800, 450}, std::pair{480, 270}})
        {
            const auto previousGeneration = renderer->Stats().swapchainGeneration;
            REQUIRE(SDL_SetWindowSize(native, width, height));
            REQUIRE(SDL_SyncWindow(native));
            renderFrames(30);
            CHECK(renderer->Stats().swapchainGeneration > previousGeneration);
            const auto pixels = window->GetFramebufferExtent();
            REQUIRE(pixels);
            CHECK(renderer->Stats().width == static_cast<std::uint32_t>(pixels->width));
            CHECK(renderer->Stats().height == static_cast<std::uint32_t>(pixels->height));
        }
        REQUIRE(SDL_MinimizeWindow(native));
        REQUIRE(SDL_SyncWindow(native));
        REQUIRE(platform->PumpEvents() == owl::platform::EventPumpResult::Continue);
        const auto beforeMinimized = renderer->Stats();
        if (replaceTextures)
            requestReplacement();
        for (int frame = 0; frame < 4; ++frame)
            CHECK(renderer->RenderFrame(error) == owl::vulkan::FrameResult::Deferred);
        CHECK(renderer->Stats().submittedFrames == beforeMinimized.submittedFrames);
        CHECK(renderer->Stats().submittedSerial == beforeMinimized.submittedSerial);
        CHECK(renderer->Stats().completedSerial == beforeMinimized.completedSerial);
        CHECK(renderer->Stats().swapchainGeneration == beforeMinimized.swapchainGeneration);
        CHECK(renderer->Stats().frameUploadCapacityBytes ==
              beforeMinimized.frameUploadCapacityBytes);
        CHECK(renderer->Stats().frameUploadedBytes == beforeMinimized.frameUploadedBytes);
        REQUIRE(SDL_RestoreWindow(native));
        REQUIRE(SDL_SyncWindow(native));
        renderFrames(120);
        const auto beforeIdle = renderer->Stats();
        REQUIRE(renderer->WaitIdle(error));
        checkDrainedMemory();
        if (replaceTextures)
        {
            CHECK(renderer->Stats().textureReplacements == acceptedReplacements);
            CHECK_FALSE(renderer->Stats().textureReplacementPending);
            CHECK(renderer->Stats().pendingTextureRetirements == 0);
        }
        CHECK(renderer->Stats().frameUploadedBytes == beforeIdle.frameUploadedBytes);
        checkUploadStats(renderer->Stats());
        CHECK(renderer->Stats().completedSerial == renderer->Stats().submittedSerial);
        CHECK(renderer->Stats().submittedSerial > initial.submittedSerial);
        CHECK(renderer->Stats().submittedFrames >= renderer->Stats().presentedFrames);
        CHECK(renderer->Stats().presentedFrames > initial.presentedFrames);
        CHECK(renderer->Stats().indexedDraws ==
              (mode != SampleMode::Clear ? renderer->Stats().submittedFrames : 0));
        if (replaceTextures)
            requestReplacement(); // Destructor must drain an accepted upload without another frame.
        renderer.reset();
        CHECK(window->IsValid());
    }
} // namespace

TEST_CASE("Vulkan clear frames resize and recover locally",
          "[vulkan][integration][frame][frame-lifecycle]")
{
    RunLocalFrames(SampleMode::Clear);
}

TEST_CASE("Vulkan triangle frames resize and recover locally",
          "[vulkan][integration][triangle][frame-lifecycle]")
{
    RunLocalFrames(SampleMode::Triangle);
}

TEST_CASE("Empty renderer rejects texture replacement", "[vulkan][texture-replacement]")
{
    owl::vulkan::VulkanTriangle renderer;
    std::string error;
    CHECK(renderer.RequestTextureReplacement({}, error) ==
          owl::vulkan::TextureReplacementResult::Failed);
    CHECK_FALSE(error.empty());
}

TEST_CASE("Single-level texture replacement survives frame lifecycle",
          "[vulkan][integration][texture-replacement][frame-lifecycle]")
{
    RunLocalFrames(SampleMode::Texture, false, true);
}

TEST_CASE("GPU mip texture replacement survives streaming and frame lifecycle",
          "[vulkan][integration][texture-replacement][frame-lifecycle][frame-upload]")
{
    RunLocalFrames(SampleMode::TextureMips, true, true);
}

TEST_CASE("CPU mip texture replacement survives streaming and frame lifecycle",
          "[vulkan][integration][texture-replacement][frame-lifecycle][frame-upload]")
{
    RunLocalFrames(SampleMode::TextureMipsCpu, true, true);
}

TEST_CASE("Vulkan mip texture frames resize and recover locally",
          "[vulkan][integration][texture][mip][frame-lifecycle]")
{
    RunLocalFrames(SampleMode::TextureMips);
}
