#include <catch2/catch_test_macros.hpp>

#include "SDLWindowAccess.h"
#include "VulkanFrame.h"
#include <SDL3/SDL.h>
#include <owl/foundation/Log.h>
#include <owl/platform/Platform.h>
#include <owl/vulkan/VulkanTriangle.h>

#include <array>
#include <chrono>
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

TEST_CASE("Vulkan clear frames resize and recover locally", "[vulkan][integration][frame]")
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
    auto window = platform->CreateWindow({.title = "OwlEngine - M1 Vulkan Clear",
                                          .width = 640,
                                          .height = 360,
                                          .resizable = true,
                                          .surfaceApi = owl::platform::WindowSurfaceApi::Vulkan},
                                         error);
    REQUIRE(window);
    auto renderer = owl::vulkan::VulkanTriangle::Create(*window, error,
                                                        {.enablePresentFences = usePresentFences});
    INFO(error);
    REQUIRE(renderer);
    REQUIRE(renderer->IsValid());
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
            const auto result = renderer->RenderFrame(error);
            INFO(error);
            REQUIRE(result != owl::vulkan::FrameResult::Failed);
            if (result == owl::vulkan::FrameResult::Deferred)
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        REQUIRE(renderer->Stats().presentedFrames == target);
        CHECK(error.empty());
    };
    renderFrames(120);
    const auto initial = renderer->Stats();
    owl::vulkan::VulkanTriangle moved{std::move(*renderer)};
    CHECK_FALSE(renderer->IsValid());
    CHECK(renderer->RenderFrame(error) == owl::vulkan::FrameResult::Failed);
    *renderer = std::move(moved);
    CHECK_FALSE(moved.IsValid());

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
    for (int frame = 0; frame < 4; ++frame)
        CHECK(renderer->RenderFrame(error) == owl::vulkan::FrameResult::Deferred);
    CHECK(renderer->Stats().submittedFrames == beforeMinimized.submittedFrames);
    CHECK(renderer->Stats().swapchainGeneration == beforeMinimized.swapchainGeneration);
    REQUIRE(SDL_RestoreWindow(native));
    REQUIRE(SDL_SyncWindow(native));
    renderFrames(120);
    REQUIRE(renderer->WaitIdle(error));
    CHECK(renderer->Stats().submittedFrames >= renderer->Stats().presentedFrames);
    CHECK(renderer->Stats().presentedFrames > initial.presentedFrames);
    renderer.reset();
    CHECK(window->IsValid());
}
