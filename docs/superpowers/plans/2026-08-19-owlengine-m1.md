# OwlEngine M1 Vulkan Bootstrap Implementation Plan

**Goal:** Add a native Vulkan 1.3 triangle sample that creates a validated instance, selects a
device and queues, owns a swapchain, recovers from resize/minimize, and presents for 10,000 frames
without validation errors.

**Design:** `docs/superpowers/specs/2026-08-19-owlengine-m1-design.md`

**Boundary:** This plan implements M1 only. It does not create an RHI, a reusable renderer,
general resource abstractions, shader compilation infrastructure, or any D3D12 code.

## Task 1: Add the Vulkan Build Dependency and Target Skeleton

**Files:**

- Modify: `vcpkg.json`
- Modify: `CMakeLists.txt`
- Create: `engine/vulkan/CMakeLists.txt`
- Create: `engine/vulkan/include/owl/vulkan/VulkanTriangle.h`
- Create: `engine/vulkan/src/VulkanTriangle.cpp`

**Steps:**

1. Add the pinned-registry `vulkan` package to the manifest; do not depend on an absolute SDK path.
2. Add the static `OwlVulkan` target after `OwlPlatform`; link `OwlFoundation`, `OwlPlatform`, and
   `Vulkan::Vulkan` privately.
3. Expose a small Owl-owned triangle lifecycle API with no public `Vk*` types.
4. Confirm `windows-vs2022` configure, Debug build, and existing CTest suite still succeed.

**Acceptance:** A clean configure obtains Vulkan Headers/Loader through vcpkg and `OwlVulkan`
builds as an empty, warning-clean target.

## Task 2: Extend Platform Only for M1 Surface and Sizing Needs

**Files:**

- Modify: `engine/platform/include/owl/platform/Platform.h`
- Modify: `engine/platform/include/owl/platform/Window.h`
- Modify: `engine/platform/src/sdl/Window.cpp`
- Create: `engine/platform/src/sdl/SDLWindowAccess.h`
- Modify: `engine/platform/CMakeLists.txt`
- Modify: `tests/platform/PlatformTests.cpp`
- Modify: `tests/platform/WindowTests.cpp`

**Steps:**

1. Add `WindowSurfaceApi::None/Vulkan` to `WindowDesc`; default to `None` and map only `Vulkan` to
   `SDL_WINDOW_VULKAN` during SDL window creation.
2. Add an Owl-owned pixel framebuffer extent query to `Window`; it returns no Vulkan type.
3. Create a narrowly named private SDL access header returning the SDL window for `OwlVulkan` only.
4. Keep the header out of public include directories and add it only to `OwlVulkan`'s private include
   path.
5. Preserve move/null-state behavior and extend tests only where SDL video initialization is not
   required.

**Acceptance:** No public Platform header exposes `SDL_Window*`, `void*`, `VkInstance`, or
`VkSurfaceKHR`; Smoke windows do not request Vulkan, Vulkan windows do, and `OwlVulkan` can obtain
the SDL window through its private boundary.

## Task 3: Create Instance, Validation, and Surface Ownership

**Files:**

- Modify: `vcpkg.json`
- Modify: `engine/vulkan/CMakeLists.txt`
- Create: `engine/vulkan/src/VulkanInstance.h`
- Create: `engine/vulkan/src/VulkanInstance.cpp`
- Create: `engine/vulkan/src/VulkanSurface.h`
- Create: `engine/vulkan/src/VulkanSurface.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/vulkan/VulkanBootstrapTests.cpp`

**Steps:**

1. Enable SDL3's vcpkg `vulkan` feature so `SDL_WINDOW_VULKAN` and the SDL surface bridge are
   compiled into the reproducible dependency build.
2. Query the Vulkan Loader instance version before creation and reject versions below Vulkan 1.3
   with the discovered native version in the error.
3. Enumerate SDL-required instance extensions and add the debug-utils extension only when validation
   is enabled.
4. Enable validation when the layer exists. Enable the debug messenger separately when
   `VK_EXT_debug_utils` also exists, and log an explicit warning for either missing capability.
5. Route debug-messenger messages into Foundation logging with severity, message ID, and object
   context.
6. Create the SDL-backed surface after the instance and destroy it before the instance.
7. Check every Vulkan result at its call site and retain the native result code in logs.
8. Add deterministic CPU tests for instance configuration and an opt-in local bootstrap test that
   creates a Vulkan window, instance, debug messenger when available, and surface. Keep the runtime
   test skipped unless explicitly enabled so CI does not require a Vulkan driver.

**Acceptance:** The opt-in local bootstrap path creates and destroys an instance and surface; it
enables the validation layer on a compatible developer machine and warns without failing when the
layer is unavailable. Full triangle sample command wiring remains Task 8.

## Task 4: Implement Testable Physical-Device and Queue Selection

**Files:**

- Modify: `engine/vulkan/CMakeLists.txt`
- Create: `engine/vulkan/src/VulkanDeviceSelection.h`
- Create: `engine/vulkan/src/VulkanDeviceSelection.cpp`
- Create: `tests/vulkan/DeviceSelectionTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Steps:**

1. Keep pure queue-family, extension, surface-format, present-mode, and extent-selection helpers
   separate from Vulkan enumeration calls.
2. Require Vulkan 1.3, `VK_KHR_swapchain`, graphics+present support, Dynamic Rendering, and
   Synchronization2.
3. Prefer one graphics/present family, but retain valid separate-family indices.
4. Add deterministic CPU tests for rejection, preference, and extent clamping cases.

**Acceptance:** Device suitability failures report their missing requirement, and CPU selection
tests run without a Vulkan device or visible window.

## Task 5: Create Device, Queues, and Swapchain Lifecycle

**Files:**

- Create: `engine/vulkan/src/VulkanDevice.h`
- Create: `engine/vulkan/src/VulkanDevice.cpp`
- Create: `engine/vulkan/src/VulkanSwapchain.h`
- Create: `engine/vulkan/src/VulkanSwapchain.cpp`
- Modify: `engine/vulkan/src/VulkanTriangle.cpp`

**Steps:**

1. Create one logical device with unique graphics/present queue create infos and only M1 feature
   chains.
2. Create the swapchain, image views, and logs for selected format, present mode, extent, and image
   count.
3. Use concurrent sharing only for separate graphics/present families.
4. Model swapchain recreation as a local operation that preserves instance, surface, physical
   device, logical device, and queues.
5. Defer recreation while the window has a zero pixel extent.

**Acceptance:** Resize, minimize, restore, and close do not make an out-of-date swapchain a fatal
error or rebuild device-level objects.

## Task 6: Add Per-Frame Synchronization and the Clear Path

**Files:**

- Create: `engine/vulkan/src/VulkanFrame.h`
- Create: `engine/vulkan/src/VulkanFrame.cpp`
- Create: `engine/vulkan/src/VulkanPresentationSupport.h`
- Modify: `engine/vulkan/include/owl/vulkan/VulkanTriangle.h`
- Modify: `engine/vulkan/src/VulkanInstance.h/.cpp`
- Modify: `engine/vulkan/src/VulkanDevice.h/.cpp`
- Modify: `engine/vulkan/src/VulkanTriangle.cpp`
- Modify: `engine/vulkan/CMakeLists.txt`
- Create: `tests/vulkan/FrameTests.cpp`

**Steps:**

1. Allocate two frame slots, each with a submission fence, acquire semaphore, acquire-completion
   fence for error-path cleanup, command pool, and primary command buffer. Allocate render-finished
   semaphores by swapchain image, not by frame slot.
2. Wait for the selected frame fence before acquire; reset its pool only after that wait.
3. Treat acquire/present out-of-date and suboptimal results as recreation requests.
4. Record explicit image layout transitions and Dynamic Rendering clear commands.
5. Prefer optional KHR/EXT swapchain-maintenance present fences when the full dependency/feature
   chain is available. Preserve the Vulkan 1.3 fallback and log its WaitIdle lifetime limitation.
6. Reset the submission fence only after successful acquisition and command recording. Track
   whether acquire, submit, and present operations were actually enqueued to avoid waiting on
   unsignaled fences on retry/failure paths.
7. Keep the public sample lifecycle free of Vulkan handles. Validate clear presentation through
   an opt-in GPU test before adding the triangle pipeline and Sandbox command in Tasks 7-8.

**Acceptance:** The sample lifecycle presents a clear color for a sustained run without per-frame
global idle waits. Resize/minimize/restore preserve device-level objects. Test both optional-fence
and explicit compatibility modes, and keep validation-layer and fallback guarantees distinct.

### Task 6 Follow-up: Interactive Clear Entry

Implemented ahead of triangle wiring to provide a persistent manual acceptance window:

- `OwlSandbox --sample clear` creates a Vulkan-intent window and uses the existing clear lifecycle.
- Sandbox owns event polling and frame pacing, including `Deferred`; teardown keeps the renderer
  before its borrowed window and keeps logging alive through cleanup.
- No arguments still run Smoke. Parser/help and a non-GUI Platform-failure test cover the new entry.
- MSVC delay-loads the Loader. The Sandbox directory uses vcpkg's PowerShell deployment path to
  include delay imports without changing deployment for other targets.
- VS2026 Debug and RelWithDebInfo build/default tests and four opt-in GPU tests pass. The Debug
  interactive entry presents and exits normally. The user reported normal manual Clear checks on
  2026-09-14. Independent capture, validation-enabled runs, and RenderDoc acceptance remain open;
  this is not triangle or full M1 acceptance.

## Task 7: Add Triangle Assets and Pipeline

**Implementation status (2026-09-14):** Implemented. Sample-local GLSL and validated SPIR-V,
host-visible indexed geometry, an empty Pipeline Layout, and a Dynamic Rendering graphics pipeline
are connected to the existing lifecycle. Dynamic viewport/scissor reuse the pipeline on extent-only
resize; format changes rebuild it after idle. CPU tests and both-configuration GPU lifecycle tests
pass. The user confirmed the triangle is visible. Remaining window checks and RenderDoc acceptance
are open; see `PROJECT.md` for current evidence.

**Files:**

- Create: `samples/owl_sandbox/assets/m1/triangle.vert.spv`
- Create: `samples/owl_sandbox/assets/m1/triangle.frag.spv`
- Create: `engine/vulkan/src/VulkanTrianglePipeline.h`
- Create: `engine/vulkan/src/VulkanTrianglePipeline.cpp`
- Modify: `samples/owl_sandbox/CMakeLists.txt`
- Modify: `engine/vulkan/CMakeLists.txt`

**Steps:**

1. Add one checked-in sample-local precompiled SPIR-V vertex/fragment pair and copy it beside the
   Debug and RelWithDebInfo executable.
2. Load shader modules with focused error messages; do not create a shader package, cache, or DXC
   build step.
3. Create a minimal indexed-triangle buffer and graphics pipeline compatible with Dynamic Rendering.
4. Recreate only pipeline state whose format or extent dependency requires it.

**Acceptance:** `OwlVulkan` renders an indexed triangle after the clear path, and a RenderDoc
capture shows the expected draw and presentation sequence.

## Task 8: Wire the Sandbox Command and Regression Tests

**Implementation status (2026-09-14):** Implemented. `--sample triangle` resolves shader files
beside the executable through Platform, and shares the Vulkan sample loop with Clear. Builds deploy
the checked-in binaries for each configuration, including missing-output recovery. Parser/help,
Platform failure, foreign-working-directory startup, and normal shutdown have been exercised.

**Files:**

- Modify: `engine/foundation/include/owl/foundation/CommandLine.h`
- Modify: `engine/foundation/src/CommandLine.cpp`
- Modify: `tests/foundation/CommandLineTests.cpp`
- Modify: `samples/owl_sandbox/src/main.cpp`
- Create: `samples/owl_sandbox/src/TriangleSample.h`
- Create: `samples/owl_sandbox/src/TriangleSample.cpp`
- Modify: `samples/owl_sandbox/CMakeLists.txt`

**Steps:**

1. Add the explicit `--sample triangle` command while preserving Smoke and the existing clear entry.
2. Keep the event loop in Sandbox policy; call one `OwlVulkan` frame operation per iteration.
3. Extend parser and executable tests for help, triangle recognition, and invalid inputs.
4. Do not run the interactive triangle sample in CTest.

**Acceptance:** Existing M0 and clear-entry tests remain green and `OwlSandbox --help` documents
all supported sample modes.

## Task 9: M1 Evidence and Acceptance Run

**Files:**

- Modify: `docs/building/windows.md`
- Modify: `README.md`
- Create: `docs/learning/m1-vulkan-bootstrap.md`

**Steps:**

1. Document new Vulkan prerequisites, optional SDK validation-layer setup, and the supported triangle
   command without hardcoding a machine SDK path.
2. Record a named GPU, driver, build configuration, validation-layer status, 10,000-frame run, and
   RenderDoc capture outcome.
3. Run Debug and RelWithDebInfo configure/build/test for the active toolchain; preserve M0 smoke
   verification.
4. Update CI only when the new target needs a portable noninteractive assertion; do not claim that
   CI proves interactive presentation.

**Acceptance:** The M1 Definition of Done in the approved design has concrete local evidence, and
the supported M0 workflow remains intact.
