# OwlEngine

OwlEngine is a long-lived, open-source personal rendering engine focused on learning modern graphics APIs, RHI design, and modern rendering architecture through production-quality code.

The project prioritizes:

1. Learning depth
2. Architecture quality
3. Engineering quality
4. Feature count

## Project Status

M0, the reproducible Windows engineering baseline, is complete. It provides the Foundation,
Platform, and Sandbox targets; a visible SDL3 smoke sample; focused CPU and executable tests; and
Windows CI coverage for Visual Studio 2022 and Visual Studio 2026.

M1, native Vulkan bootstrap and frame lifecycle, is complete for the local VS2026 acceptance scope.
Instance, Surface, physical-device selection, logical Device, queues, Swapchain, and a two-frame
clear/draw/present loop are implemented.
Opt-in local integration tests exercise Dynamic Rendering, synchronization, resize, minimize,
and restore, with optional presentation fences and a Vulkan 1.3 compatibility path. Sandbox provides
Smoke, Clear, and indexed Triangle samples. Triangle uses sample-local precompiled SPIR-V and a
native Dynamic Rendering graphics pipeline. The acquire-to-layout-transition synchronization
issue is fixed: both configurations' GPU tests and a 12,124-frame Debug run pass synchronization
validation. RenderDoc inspection and user-reported manual visual acceptance also passed.
See [M1 acceptance](docs/learning/m1-vulkan-bootstrap.md) for verification scope and workflow.

M2 is in implementation. The native M2A exercise established buffer ownership, staging uploads,
exact GPU readback, and device-local triangle geometry. M2B-1 now uses pinned VMA 3.4.0 for
allocation, with explicit host-access intent and unchanged upload synchronization. M2B-2 provides
private Image/ImageView/Sampler owners, single-mip RGBA8 uploads, exact image readback, and a
textured indexed-quad sample with one combined image sampler. The M2B-2 checkpoint passed 99/99
tests in both VS2026 configurations; a RenderDoc capture confirms sampled output.
M2B-3 adds capability-gated GPU mip blits and a CPU linear-light fallback, with per-level
readback tests. The mip sampling Demo adds fixed LOD 0-6 and automatic minification with GPU/CPU
generation modes; RenderDoc/MCP confirms fixed-LOD output.
M2C-1 gives successful frame submissions stable serials and tracks fence-observed completion,
preparing for safe reuse and retirement while frames are in flight. M2C-2 now provides CPU upload
range/reuse policies, persistent Buffer mapping and bounded upload arenas with per-slot ownership.
The two mip samples now upload CPU-scaled quad geometry each frame into separate device-local
destinations. The M2C-2 checkpoint passed 145/145 opt-in tests in both VS2026 configurations,
with synchronization validation,
including dual-slot upload stress, stale-completion rejection, stable allocation behavior and
independent streamed LOD 0/6/implicit pixel checks for both GPU and CPU mip generation. M2C-2 final
acceptance now includes user-confirmed controls, normal-exit upload statistics and RenderDoc/MCP
inspection of streamed LOD-0 frames. M2C-3 now provides a bounded CPU retirement queue and an
immutable texture binding that owns its image, view, sampler and descriptor allocation independently
of the draw pipeline. Runtime replacement now submits a bounded upload, publishes a new binding
after completion and retires the old one by its last successful graphics-use serial. Press R in
either mip sample to switch texture palettes. The M2C-3 checkpoint passed 163/163 tests in both
VS2026 configurations with actual synchronization validation and zero errors/warnings. Local acceptance
now includes user-reported controls, drained replacement counters and GPU LOD-0 capture; the
project record distinguishes missing CPU replacement captures and window-recreation evidence.
M2D-1 now exposes explicit VMA allocation/block statistics and event-triggered Sandbox logs.
The M2D-1 checkpoint passed 165/165 tests in both VS2026 configurations with synchronization
validation and zero errors/warnings. A later test cleanup reduced that suite to 153 cases;
the affected 14 Debug cases passed with validation. User-confirmed GPU/CPU mip runs returned to their startup live-allocation
baseline after 21/14 replacements, with normal exit and zero validation errors/warnings.
M2D-2 adds Buffer/Image counts, requested/allocation bytes and a retired-image subset to those logs.
Its 11 affected existing Debug cases passed with synchronization validation and zero errors/warnings;
no full-suite or manual sample replay was repeated.
M2D-3 adds optional device memory budgets and per-heap statistics. Its 15 affected existing Debug
cases and a bounded Sandbox log run passed with synchronization validation and zero errors/warnings.
M2D-4 adds an ImGui diagnostics panel to `texture-mips`: frame/upload/retirement counters,
Buffer/Image and VMA statistics, and optional heap budgets. Seven affected Debug cases passed
with actual synchronization validation and zero errors/warnings; user-reported UI acceptance
and a bounded steady-state Vulkan trace also passed. Only one event case was added; the UI
lifecycle extends the existing GPU-mip test.
See the
[memory statistics design](docs/superpowers/specs/2026-11-05-owlengine-m2d-memory-statistics-design.md),
[resource classification design](docs/superpowers/specs/2026-11-06-owlengine-m2d-resource-statistics-design.md),
[heap budget design](docs/superpowers/specs/2026-11-06-owlengine-m2d-memory-budget-design.md),
[debug panel design and evidence](docs/superpowers/specs/2026-11-09-owlengine-m2d-imgui-panel-design.md),
[M2 design](docs/superpowers/specs/2026-09-23-owlengine-m2-design.md),
[textured quad plan](docs/superpowers/plans/2026-09-30-owlengine-m2b-textured-quad.md), and
[project status](PROJECT.md) for current verification and remaining limits.

## Build, Test, and Run on Windows

Use the preset matching the installed Visual Studio version:

```powershell
# Configure with the pinned repository-local vcpkg toolchain.
./scripts/configure.ps1 -Preset windows-vs2022

# Build and test Debug. Replace vs2022 with vs2026 on that toolchain.
cmake --build --preset windows-vs2022-debug
ctest --preset windows-vs2022-debug

# Run the required visible M0 smoke sample.
./build/windows-vs2022/bin/Debug/OwlSandbox.exe --sample smoke

# Run the M1 clear sample on a Vulkan 1.3-capable machine.
./build/windows-vs2022/bin/Debug/OwlSandbox.exe --sample clear

# Run the indexed triangle with deployed precompiled shaders.
./build/windows-vs2022/bin/Debug/OwlSandbox.exe --sample triangle

# Run the textured checkerboard quad.
./build/windows-vs2022/bin/Debug/OwlSandbox.exe --sample texture

# Inspect all seven mip levels: 0-6 select LOD, A zooms, Space pauses.
./build/windows-vs2022/bin/Debug/OwlSandbox.exe --sample texture-mips
# Use --sample texture-mips-cpu to compare CPU-generated mips.
```

`texture-mips` also displays a movable, collapsible diagnostics panel. Ctrl+Tab gives the UI
keyboard focus; sample shortcuts are gated while it captures input. Click **Return keyboard to
sample** to resume them. Escape always exits. Heap values refresh every 250 ms. The section
**Renderer VMA (excludes ImGui)** counts only Owl's allocator: the official ImGui backend owns
separate GPU allocations. First use or UI/font growth can allocate and wait for a font upload;
steady-state reuse does not imply that every possible UI update is allocation-free.

The Vulkan samples print `VMA memory` at startup and after WaitIdle on exit. In either mip sample,
press R to observe allocation changes during upload, replacement and retirement. Live allocation
count/bytes should return to the startup baseline after drain; reserved block bytes can stay cached.
Buffer/Image fields distinguish logical requested bytes from actual VMA allocation bytes. Image
requested bytes include all RGBA8 mips. Retired image allocation bytes are already included in the
Image total and should return to zero after collection.
These allocation/resource fields are this allocator's totals, not whole-process VRAM usage.
When `VK_EXT_memory_budget` is available, each heap also reports capacity, heap properties and
estimated process usage/budget through VMA. Budget is a changing guideline, not remaining free
memory. Missing budget support is reported as unavailable; ordinary allocation statistics remain.
Clear has no VMA allocator, so all these queries are unavailable. The renderer's active resources
still exist at the exit snapshot.

For daily changes, build Debug and select the affected tests. Full dual-configuration regression
belongs to milestone acceptance or configuration-sensitive work. See
[test selection and retained coverage](docs/building/windows.md#choose-tests-for-the-change).

See [Windows build instructions](docs/building/windows.md) for prerequisites, clean reconfigure,
and troubleshooting guidance.

## Direction

The renderer will be developed in evidence-driven vertical slices:

1. Build a native Vulkan 1.3 reference renderer.
2. Extract an RHI from real Vulkan use cases.
3. Add a D3D12 backend and revise Vulkan-shaped abstractions.
4. Build Shader, Material, Render Graph, PBR, Bindless, GPU-Driven, and Async Compute systems.
5. Bring the renderer to Android Vulkan and study tile-based GPU optimization, bandwidth, overdraw, memory, thermal behavior, and power use.

## Planned Technical Baseline

- C++20
- CMake Presets
- Pinned vcpkg manifest dependencies
- SDL3 behind the Platform layer
- Vulkan 1.3 as the minimum desktop Vulkan contract
- D3D12 as the second desktop backend
- HLSL and DXC targeting SPIR-V and DXIL
- Android Vulkan after the desktop RHI and Render Graph are stable

## Scope

OwlEngine is a focused renderer and engine laboratory, not an attempt to reproduce a full commercial game engine. Early milestones intentionally exclude a full editor, general-purpose ECS, gameplay scripting, physics, audio, networking, console platforms, and production ray tracing.

## Roadmap

The complete M0-M15 roadmap, architecture boundaries, milestone demos, Definitions of Done, common pitfalls, and trade-offs are documented in:

- [OwlEngine Roadmap Design](docs/superpowers/specs/2026-08-17-owlengine-roadmap-design.md)

Each milestone will receive its own design and implementation plan before coding begins.

## License

An open-source license will be selected before the first public release. Until a license file is added, no license is granted by this repository.
