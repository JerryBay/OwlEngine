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
- Current implementation boundary: M1 Tasks 1-4 are present in source. Runtime bootstrap currently
  reaches Window + Vulkan Instance + Surface; physical-device selection exists but is not wired
  into that runtime path.

## Implemented Baseline

- Implemented:
  - M0 Foundation, Platform, Sandbox, CMake Presets, pinned vcpkg dependencies, tests, and Windows
    CI baseline.
  - Vulkan dependency and `OwlVulkan` target.
  - Explicit Vulkan window intent, framebuffer extent query, and private SDL surface bridge.
  - Vulkan 1.3 Instance, optional validation/debug messenger, and move-only Surface ownership.
  - Testable physical-device suitability policy covering required extensions/features, graphics
    and present queue families, surface format, present mode, and extent selection.
- Verified:
  - 2026-08-31, VS2026 Debug: `cmake --build --preset windows-vs2026-debug` succeeded.
  - 2026-08-31, VS2026 Debug: `ctest --preset windows-vs2026-debug` reported 31 passed, 1 skipped,
    and 0 failed. The skipped test is the opt-in local Vulkan Window + Instance + Surface bootstrap.
- Unverified:
  - The current revision on VS2022; validate it on the separate VS2022 computer.
  - Real-GPU physical-device selection through the runtime bootstrap.
  - Logical-device, queue, swapchain, frame synchronization, resize recovery, clear, triangle,
    10,000-frame validation run, and RenderDoc acceptance.

## Milestones

| Milestone | Delivery | Validation | Completion condition |
| --- | --- | --- | --- |
| M0 Reproducible Engineering Baseline | Complete | Current VS2026 Debug build/tests pass; cross-toolchain support is defined by presets and CI | Preserve clean-clone configure, build, test, and smoke workflows |
| M1 Vulkan Bootstrap and Frame Lifecycle | In progress: Tasks 1-4 implemented | CPU tests pass; local GPU bootstrap is opt-in and was skipped in the current run | Triangle, resize/minimize recovery, 10,000 validation-clean frames, and RenderDoc capture |
| M2-M15 | Planned | Unverified | Follow the approved milestone roadmap and per-milestone design gates |

## Decisions and Constraints

- Confirmed:
  - Keep native Vulkan calls visible through the reference renderer; extract RHI only after real
    Vulkan call sites exist, then use D3D12 to challenge Vulkan-shaped assumptions.
  - Vulkan 1.3, Dynamic Rendering, and Synchronization2 are the M1 desktop baseline.
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
  - There is no logical `VulkanDevice`, queue ownership, or `VulkanSwapchain` implementation yet.

## Entry Points and Evidence

- Authoritative roadmap: `docs/superpowers/specs/2026-08-17-owlengine-roadmap-design.md`
- Active milestone design: `docs/superpowers/specs/2026-08-19-owlengine-m1-design.md`
- Active implementation plan: `docs/superpowers/plans/2026-08-19-owlengine-m1.md`
- Build documentation: `README.md` and `docs/building/windows.md`
- Build/test definitions: `CMakePresets.json`, root/module `CMakeLists.txt`, and
  `tests/CMakeLists.txt`
- Current validation evidence: the VS2026 Debug build and CTest commands recorded above.

## Next Work

- Next task: M1 Task 5, implement `VulkanDevice`, unique graphics/present queue creation, and
  `VulkanSwapchain` lifecycle, then connect `VulkanDeviceSelection` to the runtime bootstrap.
- Acceptance condition: create the logical device and swapchain with correct lifetime ordering,
  support unified and separate queue families, keep zero-size windows non-fatal, and localize
  swapchain recreation without rebuilding Instance, Surface, physical-device selection, or Device.
