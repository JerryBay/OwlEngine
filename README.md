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

M1, native Vulkan bootstrap and frame lifecycle, is in progress. Instance, Surface, physical-device
selection, logical Device, and queues are implemented and exercised by opt-in local integration
tests. Swapchain and presentation are next; Sandbox currently provides the smoke sample.
See [project status](PROJECT.md) for verification scope and the next task.

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
```

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
