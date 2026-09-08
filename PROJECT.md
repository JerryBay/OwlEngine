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
- Current implementation boundary: M1 Tasks 1-4 and Task 5's Device/queue step are implemented.
  Local integration tests reach Window + Instance + Surface + physical-device selection + Device
  and queues. Sandbox still runs only the smoke sample; Swapchain and presentation remain pending.

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
- Verified:
  - 2026-09-07, VS2026 Debug and RelWithDebInfo: both build presets succeeded; each default CTest
    preset reported 35 passed, 2 opt-in GPU tests skipped, and 0 failed.
  - User confirmation received 2026-09-07: the opt-in local Vulkan Window + Instance + Surface
    bootstrap test passed.
  - 2026-09-07, VS2026 Debug: both GPU tests passed with `OWL_RUN_VULKAN_BOOTSTRAP_TEST=1`.
    Device creation, queue retrieval, move construction, replacement, and destruction passed
    32 assertions on NVIDIA GeForce RTX 5060 (reported Vulkan API 1.4.341, unified family 0).
    The loader reported 1.4.350; the validation layer was unavailable, so this is runtime evidence
    without validation-layer coverage.
- Unverified:
  - The current revision on VS2022; validate it on the separate VS2022 computer.
  - GPU creation on hardware with separate graphics/present families (CPU policy tests cover it),
    a validation-enabled GPU run, and GPU tests in RelWithDebInfo.
  - Swapchain, frame synchronization, resize recovery, clear, triangle,
    10,000-frame validation run, and RenderDoc acceptance.

## Milestones

| Milestone | Delivery | Validation | Completion condition |
| --- | --- | --- | --- |
| M0 Reproducible Engineering Baseline | Complete | VS2026 Debug and RelWithDebInfo build/tests pass; cross-toolchain support is defined by presets and CI | Preserve clean-clone configure, build, test, and smoke workflows |
| M1 Vulkan Bootstrap and Frame Lifecycle | In progress: Tasks 1-4 and Device/queues implemented | CPU tests pass; Debug GPU bootstrap through Device/queues passes without Validation Layer | Triangle, resize/minimize recovery, 10,000 validation-clean frames, and RenderDoc capture |
| M2-M15 | Planned | Unverified | Follow the approved milestone roadmap and per-milestone design gates |

## Decisions and Constraints

- Confirmed:
  - Keep native Vulkan calls visible through the reference renderer; extract RHI only after real
    Vulkan call sites exist, then use D3D12 to challenge Vulkan-shaped assumptions.
  - Vulkan 1.3, Dynamic Rendering, and Synchronization2 are the M1 desktop baseline.
  - `VulkanDevice` owns only its logical device and borrows its queues. Its Instance must outlive
    it; before destruction or replacement the caller must finish GPU work, destroy child objects,
    and exclude concurrent host access. Destruction performs no implicit idle wait.
  - Support both VS2022 and VS2026 through separate CMake Presets; validate each on its available
    computer rather than requiring duplicate third-party source versions.
  - Use pinned vcpkg manifest dependencies. SDL3 remains behind `OwlPlatform`.
  - Update this file only for durable phase, capability, contract, workflow, validation, blocker,
    or next-task changes; do not use it as a chronological discussion log.
- Open:
  - Select an open-source license before the first public release.
- Known limitations:
  - Device selection currently returns the first suitable physical device; there is no adapter
    scoring, discrete-GPU preference, or user override.
  - Surface format and present mode are initial selection snapshots; robust swapchain recreation
    must re-query surface-dependent capabilities where required.
  - `VulkanSwapchain`, rendering submission, presentation, and Sandbox triangle wiring are pending.

## Entry Points and Evidence

- Authoritative roadmap: `docs/superpowers/specs/2026-08-17-owlengine-roadmap-design.md`
- Active milestone design: `docs/superpowers/specs/2026-08-19-owlengine-m1-design.md`
- Active implementation plan: `docs/superpowers/plans/2026-08-19-owlengine-m1.md`
- Build documentation: `README.md` and `docs/building/windows.md`
- Build/test definitions: `CMakePresets.json`, root/module `CMakeLists.txt`, and
  `tests/CMakeLists.txt`
- Device contract and tests: `engine/vulkan/src/VulkanDevice.h`, `tests/vulkan/DeviceTests.cpp`,
  and `tests/vulkan/VulkanBootstrapTests.cpp`
- Current validation evidence: the VS2026 build/CTest and Debug GPU results recorded above.
  Reproduce the GPU checks with the opt-in commands in `docs/building/windows.md`.

## Next Work

- Next task: the remaining M1 Task 5 work, implement `VulkanSwapchain`, swapchain images/views,
  surface capability queries, and local recreation using the existing Device/queues.
- Acceptance condition: create the swapchain with correct lifetime ordering,
  support unified and separate queue families, keep zero-size windows non-fatal, and localize
  swapchain recreation without rebuilding Instance, Surface, physical-device selection, or Device.
