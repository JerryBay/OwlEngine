---
project_memory: true
workflow_version: 1
---

# OwlEngine Project Memory

This file is the concise, rolling project-status entry. Current source, build definitions,
runtime evidence, and approved designs take precedence if they conflict with this summary.

## Goal and Current Phase

- Goal: Build a long-lived open-source rendering engine for learning modern graphics APIs, RHI
  design, and modern rendering architecture through production-quality code.
- Priority: Learning depth > architecture quality > engineering quality > feature count.
- Current phase: M1, native Vulkan 1.3 bootstrap and frame lifecycle.
- Current implementation boundary: M1 Tasks 1-8 are implemented through indexed-triangle presentation.
  Local integration tests reach the full native Vulkan frame lifecycle and window-driven
  recreation. Sandbox provides Smoke, Clear, and Triangle samples. Visual, validation-enabled, and
  RenderDoc acceptance remain separate checks. This is not full M1 acceptance.

## Implemented Baseline

- Implemented:
  - M0 Foundation, Platform, Sandbox, CMake Presets, pinned vcpkg dependencies, tests, and Windows
    CI baseline.
  - Vulkan dependency and `OwlVulkan` target.
  - Explicit Vulkan window intent, framebuffer extent query, and private SDL surface bridge.
  - Vulkan 1.3 Instance, optional validation/debug messenger, and move-only Surface ownership.
  - Testable physical-device suitability policy covering required extensions/features, graphics
    and present queue families, surface format, present mode, and extent selection.
  - Move-only `VulkanDevice`, unique queue-family requests, M1 feature/extension enablement,
    borrowed graphics/present queue handles, and an opt-in real-GPU lifetime test in `OwlUnitTests`.
  - Move-only `VulkanSwapchain`, fresh Surface capability/format/mode queries, image views,
    local recreation, zero-extent deferral, and opt-in real-GPU resource lifecycle tests.
  - `VulkanTriangle` lifecycle: two frame slots, command pools/buffers, acquire, Dynamic Rendering
    clear and optional indexed draw, Synchronization2 submit, and present. Slot acquisition and
    submission fences track cleanup independently; present semaphores belong to swapchain images.
  - Optional KHR/EXT swapchain-maintenance feature/dependency negotiation and per-image present
    fences, with a diagnostic option to force the compatibility fallback. Window resize and
    out-of-date results request recreation; minimized/hidden/zero-size windows defer rendering.
  - `OwlSandbox --sample clear`: persistent resizable Vulkan window, event polling, paced frames,
    Escape/close handling, reported errors, and renderer-before-window teardown. Default command
    remains Smoke. MSVC delay-load and app-local deployment preserve non-Vulkan startup paths.
  - `--sample triangle`, sample-local GLSL/precompiled SPIR-V, host-visible vertex/index geometry,
    and a private `VulkanTrianglePipeline`. Dynamic viewport/scissor preserve the pipeline across
    extent-only changes; color-format changes rebuild it after idle. No shader system or RHI.
  - `Platform::ExecutableDirectory()` supports executable-relative Sandbox asset lookup. Both
    Vulkan samples share the Sandbox event loop; Clear has no shader asset requirement.
- Verified:
  - 2026-09-14, VS2026 Debug and RelWithDebInfo: both build presets succeeded; each default CTest
    preset reported 58 passed, 5 opt-in GPU tests skipped, and 0 failed.
  - Both configurations passed all five GPU tests with `OWL_RUN_VULKAN_BOOTSTRAP_TEST=1` on
    NVIDIA GeForce RTX 5060 (reported API 1.4.341, unified family 0; loader 1.4.350).
    Swapchain checks cover 320x180, 640x360, and 480x270, moves including live-owner replacement,
    preflight failure preservation, explicit zero-size deferral on minimize, and recreation on
    restore. Each created swapchain had 3 images/views; device-level objects remained unchanged.
  - Clear and triangle tests each presented 300 frames per mode with automatic present fences enabled
    and explicitly disabled: 640x360, resize to 800x450 and 480x270, minimize without submission,
    restore, move, idle, and cleanup. Both modes passed in both configurations.
  - Catch2 discovery now uses `PRE_TEST`: after building both configurations, CTest JSON listings
    and verbose GPU runs confirm each preset selects its own executable. The previous shared
    post-build list could select the last-built configuration instead of the requested one.
  - Validation Layer was unavailable during the 2026-09-14 runs; those GPU passes do not establish
    validation coverage. See the newer validation-enabled result below.
  - The formal Debug Clear entry presented 4,980 frames and exited with code 0 via Escape.
    Its loaded module was the app-local Vulkan Loader; both deployed configurations' DLL hashes
    matched their vcpkg package DLLs. A running Debug Smoke process had no Vulkan Loader module
    and exited with code 0 through the window-close path.
  - The user previously confirmed the temporary 640x360 green clear probe visually.
  - 2026-09-14: the user reported normal results for the formal Clear sample's manual checks.
    This records user-reported acceptance, not an independent capture or validation-layer run.
  - Formal Debug Triangle launched from a different working directory, presented 10,820 frames,
    and exited with code 0 via Escape without Validation Layer. Missing deployed fragment SPIR-V
    reported its path and returned code 4; rebuilding restored the generated asset. Both runtime
    configurations' deployed shader hashes matched the checked-in assets.
  - GLSL compiled and validated using pinned glslang 16.5.0 for Vulkan 1.3/SPIR-V 1.6. Stage linking
    and repeated binary hashes passed; source, binary, and regeneration instructions are checked in.
  - 2026-09-14: the user confirmed the triangle is visible in the formal sample. This establishes
    user-reported visual output, not independent pixel inspection or validation-layer coverage.
  - 2026-09-23, VS2026 Debug: RenderDoc 1.46 captured and replayed the formal Clear sample with
    the expected green output. The existing MCP's 1.43 replay runtime rejected that newer format.
  - RenderDoc 1.43 then captured the formal Triangle sample on NVIDIA GeForce RTX 5060. The target
    loaded only the intended 1.43 capture DLL and closed normally with exit code 0. Existing MCP
    opened the capture, reported one indexed draw (3 indices, 1 instance), queried VS/PS reflection
    and the 1280x720 sRGB target, and exported the expected colored triangle. Pixel reads passed.
    Capture used RenderDoc target control; MCP file replay/query/export passed, not its automatic
    launch tool. No captured debug messages were reported; this does not establish validation-layer
    coverage. Capture-layer selection was process-local; system registration was not changed.
  - 2026-09-23, VS2026: configure and Debug/RelWithDebInfo builds succeeded. Each default CTest
    preset passed 58 tests and skipped the five opt-in GPU tests. With Khronos Validation Layer
    1.4.357 and synchronization/submit-time validation enabled, each configuration passed all five
    GPU test cases with zero validation errors and warnings after correcting the acquire-to-layout
    transition dependency. Tests cover automatic presentation fences and forced compatibility mode.
  - CTest now rejects `[error] [VulkanValidation]` output. The rule rejected the original
    RelWithDebInfo binary's Clear/Triangle tests with exit code 8, then passed with exit code 0
    after rebuilding the correction. Direct test/Sandbox execution still requires log inspection.
  - The corrected formal Debug Triangle presented 12,124 frames/indexed draws and exited with
    code 0; Clear presented 707 frames and Smoke also exited with code 0. Programmatic resize,
    maximize, minimize, restore, and close/Escape completed; both Vulkan samples reached swapchain
    generation 5. Complete logs contained zero validation errors or warnings. Recording overlays
    were disabled for these diagnostic processes; SDK layer registration was not changed.
- Unverified:
  - The current revision on VS2022; validate it on the separate VS2022 computer.
  - GPU creation on hardware with separate graphics/present families (CPU policy tests cover it).
  - Native resource/submit/fence failure injection and driver-returned out-of-date recovery;
    policy branches and synchronization/lifetime code are tested/reviewed, not fault-injected.
  - Complete client-edge inspection during continuous manual dragging. Programmatic window
    recovery and a 10,000-frame validation-clean run are verified; partial desktop snapshots do
    not certify every border pixel. The existing RenderDoc/MCP capture predates the barrier fix.
  - Native non-coherent flush and live color-format replacement paths: policy/static review covers
    them, but this GPU selected coherent memory and resizing retained the color format.

## Milestones

| Milestone | Delivery | Validation | Completion condition |
| --- | --- | --- | --- |
| M0 Reproducible Engineering Baseline | Complete | VS2026 Debug and RelWithDebInfo build/tests pass; cross-toolchain support is defined by presets and CI | Preserve clean-clone configure, build, test, and smoke workflows |
| M1 Vulkan Bootstrap and Frame Lifecycle | Implemented through indexed triangle and rendered window recovery; final visual checks pending | Both configurations' CPU/GPU tests and Debug 12,124-frame synchronization-validation run pass; earlier RenderDoc/MCP inspection passes | Visual triangle acceptance, 10,000 validation-clean frames, and RenderDoc capture |
| M2-M15 | Planned | Unverified | Follow the approved milestone roadmap and per-milestone design gates |

## Decisions and Constraints

- Confirmed:
  - Keep native Vulkan calls visible through the reference renderer; extract RHI only after real
    Vulkan call sites exist, then use D3D12 to challenge Vulkan-shaped assumptions.
  - Vulkan 1.3, Dynamic Rendering, and Synchronization2 are the M1 desktop baseline.
  - `VulkanDevice` owns only its logical device and borrows its physical device and queues. Its Instance must outlive
    it; before destruction or replacement the caller must finish GPU work, destroy child objects,
    and exclude concurrent host access. Destruction performs no implicit idle wait.
  - `VulkanSwapchain` owns its swapchain and views, not the images or parents. Recreate/destroy
    requires completed use, released external dependents, and no concurrent access; no implicit wait.
    `Deferred` preserves resources but suspends acquisition. Preflight failures preserve resources;
    once `vkCreateSwapchainKHR` is called, the old chain is retired and destroyed even on failure.
  - `VulkanTriangle` owns renderer objects and waits before replacement/destruction; its borrowed
    Window must outlive it and remain unmoved. All renderer calls belong to the window thread.
    The caller pumps events and throttles deferred frames; no per-frame device-idle wait is used.
  - Keep Vulkan 1.3 as the minimum. Present fences are optional, with KHR preferred over EXT.
    On unsupported devices, use the approved device-idle recreation/shutdown fallback and report
    its presentation-resource completion guarantee limitation rather than claiming strict WSI safety.
  - Support both VS2022 and VS2026 through separate CMake Presets; validate each on its available
    computer rather than requiring duplicate third-party source versions.
  - Use pinned vcpkg manifest dependencies. SDL3 remains behind `OwlPlatform`.
  - Update this file only for durable phase, capability, contract, workflow, validation, blocker,
    or next-task changes; do not use it as a chronological discussion log.
  - Maintain `docs/learning/graphics-api-notes.md` for transferable low-level graphics API concepts.
    Consolidate mechanisms and API differences, not individual questions, project details, or
    inferred learner assessments. Consult relevant topics before explanations; update only for
    durable knowledge and respect explicit read-only requests.
- Open:
  - Select an open-source license before the first public release.
- Known limitations:
  - Device selection currently returns the first suitable physical device; there is no adapter
    scoring, discrete-GPU preference, or user override.
  - Device-selection surface preferences are snapshots; Swapchain creation/recreation re-queries
    current Surface support instead of trusting those snapshots.
  - Minimized windows can retain a nonzero framebuffer size in SDL; the renderer checks window
    flags as well as pixel size. Surface/device loss is terminal; automatic recovery is deferred.
  - Presentation fences establish resource release, not display scanout completion. Compatibility
    fallback runs successfully locally but lacks the same specification-level release guarantee.
  - Triangle geometry uses a small immutable host-visible allocation; staging/upload allocation,
    general Shader System, and RHI are intentionally deferred.

## Entry Points and Evidence

- Authoritative roadmap: `docs/superpowers/specs/2026-08-17-owlengine-roadmap-design.md`
- Active milestone design: `docs/superpowers/specs/2026-08-19-owlengine-m1-design.md`
- Active implementation plan: `docs/superpowers/plans/2026-08-19-owlengine-m1.md`
- Build documentation: `README.md` and `docs/building/windows.md`
- Build/test definitions: `CMakePresets.json`, root/module `CMakeLists.txt`, and
  `tests/CMakeLists.txt`
- Device contract and tests: `engine/vulkan/src/VulkanDevice.h`, `tests/vulkan/DeviceTests.cpp`,
  and `tests/vulkan/VulkanBootstrapTests.cpp`
- Swapchain contract and tests: `engine/vulkan/src/VulkanSwapchain.h` and
  `tests/vulkan/SwapchainTests.cpp`
- Frame lifecycle and tests: `engine/vulkan/include/owl/vulkan/VulkanTriangle.h`,
  `engine/vulkan/src/VulkanTriangle.cpp`, `engine/vulkan/src/VulkanFrame.h`, and
  `tests/vulkan/FrameTests.cpp`
- Interactive clear entry: `samples/owl_sandbox/src/ClearSample.cpp`; launch and manual checks
  are documented in `docs/building/windows.md`.
- Triangle entry/resources: `samples/owl_sandbox/src/TriangleSample.cpp`,
  `engine/vulkan/src/VulkanTrianglePipeline.cpp`, `samples/owl_sandbox/assets/m1/README.md`, and
  `docs/learning/m1-triangle.md`.
- Low-level graphics API concepts: [graphics API notes](docs/learning/graphics-api-notes.md).
  Keep engineering details and project validation status in their existing documents.
- Current validation evidence: the VS2026 build/CTest and both-configuration GPU results above.
  Reproduce the GPU checks with the opt-in commands in `docs/building/windows.md`.
  Local RenderDoc 1.43 capture, MCP-exported PNG, and inspection results are under `build/diagnostics/`.
- Validation setup and acceptance findings: `docs/learning/m1-vulkan-bootstrap.md`; detailed
  2026-09-23 logs are in `build/diagnostics/m1-validation-20260923/` (local, ignored). Use
  `fixed-gpu/`, `fixed-sandbox/`, and `fixed-verification.md` for the corrected implementation.

## Next Work

- Next task: finish the remaining manual window visual checks and consolidate M1 acceptance.
  Preserve the RenderDoc/MCP inspection baseline; M2 resource/memory/upload design follows M1.
- Keep the native Vulkan baseline and the existing clear/smoke regressions. VS2022 validation
  belongs to the separate workstation. Do not mark M1 complete from build or unvalidated GPU passes.
