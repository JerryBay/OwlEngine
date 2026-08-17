# OwlEngine M0 Reproducible Engineering Baseline Design

- Status: Approved direction, pending written-spec review
- Date: 2026-08-17
- Parent roadmap: [OwlEngine Roadmap Design](2026-08-17-owlengine-roadmap-design.md)
- Implementation status: Not started

## 1. Objective

M0 establishes the smallest maintainable native project that proves OwlEngine can be configured, built, tested, and run from a fresh clone without machine-specific repository changes.

M0 does not contain graphics API code. Its product is a reproducible engineering path and a minimal platform smoke executable that later milestones can extend without reorganizing the repository.

The intended local acceptance path is:

```powershell
git clone <repository-url>
Set-Location OwlEngine
./scripts/configure.ps1 -Preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
./build/windows-msvc/bin/Debug/OwlSandbox.exe --sample smoke
```

The configure script is part of the supported workflow. It bootstraps the pinned repository-local vcpkg toolchain before invoking the selected CMake configure preset.

## 2. Scope

### 2.1 Included

- C++20 project baseline
- Win64 and Visual Studio 2022/MSVC primary toolchain
- CMake targets and presets
- Pinned vcpkg manifest dependencies
- Repository-local dependency bootstrap
- Foundation logging, assertions, and build information
- SDL3-backed Platform initialization and window ownership
- `OwlSandbox --sample smoke`
- Focused CPU tests and executable command-line tests
- GitHub Actions configure/build/test job
- Formatting, warnings, and project build options
- Windows clean-clone build documentation

### 2.2 Excluded

- Vulkan, D3D12, DXGI, or graphics-device initialization
- RHI, Renderer, Render Graph, Shader, Material, Asset, or Tool targets
- Math and scene libraries
- ImGui
- Runtime plugin systems
- General task systems or job schedulers
- Editor functionality
- Packaging and installer generation
- Android builds
- Performance benchmarks beyond build and startup diagnostics

The absence of empty future-module directories is deliberate. A module is added only when its owning milestone supplies real behavior.

## 3. Approach Decision

Three project-structure approaches were considered.

### 3.1 Single executable

Put command-line parsing, SDL lifecycle, logging, and the smoke loop directly in `OwlSandbox`.

Advantages:

- Lowest initial file and target count
- Fastest path to a visible window

Rejected because:

- Platform ownership would immediately mix with application policy.
- M1 would need to extract boundaries while adding Vulkan lifecycle complexity.
- Foundation tests would require linking the application target or duplicating code.

### 3.2 Full future engine skeleton

Create Foundation, Platform, RHI, Renderer, Shader, Asset, Tools, Editor, and Sample targets immediately.

Advantages:

- The repository visually resembles a complete engine from day one.
- Future directories appear to have predetermined homes.

Rejected because:

- Empty targets encode untested ownership assumptions.
- Early interfaces would be designed without call sites.
- Maintenance and CI cost would grow before delivering learning value.

### 3.3 Minimal three-target spine

Create only `OwlFoundation`, `OwlPlatform`, and `OwlSandbox`.

Advantages:

- Establishes a real dependency direction.
- Keeps SDL and platform lifecycle out of application policy.
- Gives logging, argument handling, and build information focused test seams.
- Leaves Vulkan and RHI boundaries for the milestones that own them.

Decision: use the minimal three-target spine.

## 4. Repository Layout

M0 creates the following maintained structure:

```text
OwlEngine/
|-- .github/
|   `-- workflows/
|       `-- build-windows.yml
|-- cmake/
|   |-- OwlOptions.cmake
|   `-- OwlWarnings.cmake
|-- docs/
|   |-- building/
|   |   `-- windows.md
|   `-- superpowers/specs/
|-- engine/
|   |-- foundation/
|   |   |-- include/owl/foundation/
|   |   |-- src/
|   |   `-- CMakeLists.txt
|   `-- platform/
|       |-- include/owl/platform/
|       |-- src/sdl/
|       `-- CMakeLists.txt
|-- samples/
|   `-- owl_sandbox/
|       |-- src/
|       `-- CMakeLists.txt
|-- scripts/
|   |-- bootstrap-vcpkg.ps1
|   `-- configure.ps1
|-- tests/
|   |-- foundation/
|   `-- platform/
|-- .clang-format
|-- CMakeLists.txt
|-- CMakePresets.json
|-- vcpkg-configuration.json
`-- vcpkg.json
```

Headers use the `owl/...` include prefix. CMake target names use the `Owl` prefix. C++ code uses the `owl` namespace.

## 5. Target Ownership and Dependencies

```text
OwlSandbox
    |
OwlPlatform ---- SDL3
    |
OwlFoundation -- spdlog

OwlFoundationTests -- Catch2 -- OwlFoundation
OwlPlatformTests   -- Catch2 -- OwlPlatform
```

### 5.1 OwlFoundation

Owns:

- Logging initialization, shutdown, levels, and named loggers
- Development assertions
- Build configuration, compiler, version, and Git revision reporting
- Small command-line parsing needed by `OwlSandbox`

Does not own:

- Containers, memory allocators, math, filesystem abstraction, threading, reflection, or a general service locator

`spdlog` is a private implementation dependency. Public OwlFoundation headers must not require consumers to include spdlog headers.

### 5.2 OwlPlatform

Owns:

- SDL subsystem initialization and shutdown
- A move-only window owner
- Event polling required by the smoke sample
- Platform and display information used by diagnostics

Does not own:

- Frame timing policy
- Application state transitions
- Rendering surfaces or graphics API objects
- Input mapping or gameplay input
- A public raw `SDL_Window*` escape hatch

M1 decides how Vulkan surface creation crosses the Platform/backend boundary. M0 must not preempt that design with a generic `void*` native handle.

### 5.3 OwlSandbox

Owns:

- Process entrypoint
- Command-line mode selection
- Smoke-sample policy and lifetime
- Exit-code mapping

The executable contains no reusable engine implementation. Reusable behavior moves downward only when a current call site justifies it.

## 6. Dependency Management

### 6.1 M0 manifest

The M0 vcpkg manifest contains only:

- SDL3
- spdlog
- Catch2

Vulkan headers, Vulkan loader, Vulkan Memory Allocator, GLM, DXC, ImGui, and asset libraries are not M0 dependencies.

### 6.2 Pinning

- `vcpkg-configuration.json` records a builtin registry baseline.
- The repository records the vcpkg tool revision consumed by the bootstrap script.
- Dependency version changes are intentional commits with configure/build/test evidence.
- Floating branches and unpinned archives are not allowed in supported configure paths.

### 6.3 Bootstrap behavior

`scripts/bootstrap-vcpkg.ps1`:

1. Resolves the repository root from the script location.
2. Uses a repository-local ignored tools directory.
3. Clones the recorded vcpkg revision when absent.
4. Verifies an existing checkout matches the recorded revision.
5. Bootstraps the vcpkg executable when required.
6. Returns a nonzero exit code with an actionable message on failure.

`scripts/configure.ps1` invokes the bootstrap script and then calls `cmake --preset <name>` with the resolved toolchain path. It does not permanently modify user environment variables.

The scripts do not install Visual Studio, Git, CMake, or system drivers. These prerequisites are detected and documented.

## 7. CMake Design

### 7.1 Minimum contract

- CMake 3.28 or newer
- C++20 required, with compiler extensions disabled
- Visual Studio 2022/MSVC primary local toolchain
- 64-bit Windows only in M0
- Static Owl libraries; no shared-library option in M0

### 7.2 Presets

Supported configure preset:

- `windows-msvc`: Visual Studio 17 2022, x64, repository-local binary directory

Supported build presets:

- `windows-msvc-debug`
- `windows-msvc-relwithdebinfo`

Supported test presets:

- `windows-msvc-debug`
- `windows-msvc-relwithdebinfo`

`CMakeUserPresets.json` remains ignored for developer-local overrides. Supported workflows cannot depend on it.

### 7.3 Project options

M0 defines a small set of namespaced options:

- `OWL_BUILD_TESTS`, enabled for local development and CI
- `OWL_WARNINGS_AS_ERRORS`, enabled in CI for Owl-owned targets

Options for renderer backend selection, shared libraries, unity builds, sanitizers, profiling, or shipping configurations are not added until required.

### 7.4 Warning policy

- Warning flags apply through an internal interface target to Owl-owned targets only.
- Third-party targets do not inherit Owl warnings or warnings-as-errors.
- MSVC uses a high warning level, standards-conformance mode, and correct `__cplusplus` reporting.
- The build does not globally rewrite compiler flags supplied by toolchains or consumers.

## 8. Smoke Sample Contract

### 8.1 Command-line behavior

`OwlSandbox` supports:

- `--help`: prints supported samples and exits successfully without initializing video.
- `--sample smoke`: runs the visible M0 smoke sample.
- Unknown or incomplete arguments: print a focused error plus help hint and return a nonzero exit code.

No generic console framework or reflection-based sample registry is introduced. A small explicit dispatch table or equivalent direct logic is sufficient for the initial executable.

### 8.2 Visible behavior

The smoke sample:

1. Initializes logging.
2. Logs OwlEngine revision, build configuration, compiler, OS, CPU architecture, and SDL version.
3. Initializes the SDL video subsystem.
4. Opens one resizable 1280 x 720 window titled `OwlEngine - M0 Smoke`.
5. Pumps events until the user closes the window or presses Escape.
6. Destroys the window, shuts down SDL, flushes logging, and exits with code zero.

The sample does not create a graphics context, Vulkan surface, renderer thread, or fixed game loop.

### 8.3 Failure behavior

- Initialization failures include the failing operation and SDL diagnostic text.
- Partial initialization is unwound in reverse ownership order.
- The process returns a stable nonzero exit code for invalid arguments or initialization failure.
- M0 does not introduce a general-purpose `Result<T>` abstraction. Local status and error handling remain explicit and narrow.

## 9. Logging, Assertions, and Build Information

### 9.1 Logging

- Foundation exposes Owl-owned log entrypoints without exposing spdlog types.
- Development output includes time, level, and logger category.
- Logging initialization is explicit and idempotent for tests.
- Shutdown flushes pending messages.
- File logging and asynchronous logging are deferred until a real runtime need exists.

### 9.2 Assertions

- Assertions identify expression, file, line, and optional context.
- Debug and RelWithDebInfo keep development assertions enabled.
- Assertions are for broken invariants, not recoverable platform errors.
- No custom crash handler is added in M0.

### 9.3 Build information

Build information is generated by CMake and includes:

- Project version
- Git revision when available
- Build configuration
- Compiler identity and version
- Target operating system and architecture

A source archive without Git metadata reports a stable `unknown` revision rather than failing configuration.

## 10. Tests

### 10.1 CPU unit tests

M0 tests cover deterministic behavior:

- Supported command-line combinations
- Unknown and incomplete argument handling
- Build-information fields
- Repeated logging initialization and shutdown
- Move ownership and null-state behavior for Platform wrappers where this does not require a visible window

### 10.2 Executable tests

CTest runs:

- `OwlSandbox --help`
- An invalid-argument invocation that is expected to fail with the documented exit code

CI does not run the visible window sample because the hosted environment is not the acceptance path for interactive video initialization.

### 10.3 Manual smoke test

The visible `--sample smoke` run is a required local acceptance step. The result records:

- Command used
- Window created and responsive
- Escape and close-button shutdown behavior
- Exit code
- Logged build/platform information

## 11. Continuous Integration

The initial GitHub Actions workflow runs on a supported Windows image and performs:

1. Checkout
2. Dependency bootstrap and CMake configure
3. Debug build
4. Debug CTest run with failure output
5. RelWithDebInfo build
6. RelWithDebInfo CTest run with failure output

CI may cache downloaded vcpkg artifacts, but cache hits are not required for correctness. A cache miss must still produce a complete build.

M0 CI does not claim GPU, Vulkan, GUI, Android, packaging, or multi-compiler coverage.

## 12. Documentation

`docs/building/windows.md` documents:

- Required Git, CMake, and Visual Studio C++ workload prerequisites
- Supported configure, build, test, and run commands
- Repository-local vcpkg bootstrap behavior
- How to delete build output and reconfigure without deleting source files
- Common configuration failures with actionable checks
- The distinction between successful CI tests and the required local visible smoke run

README keeps its current project-status warning until M0 passes the clean-clone acceptance procedure. Only then may README advertise the supported build commands.

## 13. Definition of Done

M0 is complete only when all of the following are true:

1. The committed repository contains only the M0 targets and dependencies defined in this spec.
2. `scripts/configure.ps1 -Preset windows-msvc` succeeds from a fresh clone with documented prerequisites.
3. Debug and RelWithDebInfo builds succeed through their named presets.
4. Both CTest presets report zero failures.
5. `OwlSandbox --help` succeeds without initializing a window.
6. `OwlSandbox --sample smoke` opens the specified responsive window and exits cleanly through Escape and the close button.
7. Logs report correct build and platform information.
8. Owl-owned targets compile cleanly under the configured warning policy.
9. GitHub Actions passes from a dependency-cache miss.
10. A second clean directory or machine repeats configure, build, test, and visible run without source edits or absolute-path fixes.
11. README build instructions are updated only after the preceding evidence exists.
12. No Vulkan, RHI, Renderer, Shader, Material, Asset, or Editor implementation has entered the change.

Build success alone does not prove the visible smoke sample or clean-machine workflow.

## 14. Risks and Trade-offs

### Repository-local vcpkg checkout

Cost: initial download time and local disk usage.

Reason accepted: it gives the supported configure script a known tool revision and avoids relying on a mutable machine-global installation.

### Visual Studio generator as the only M0 generator

Cost: the first milestone does not prove Ninja or clang-cl support.

Reason accepted: one fully reproducible primary path is more valuable than multiple partially verified paths. Additional compilers require their own milestone evidence.

### SDL3 behind OwlPlatform

Cost: a small wrapper must be maintained and some SDL capabilities remain intentionally unavailable.

Reason accepted: Platform owns lifecycle and prevents SDL types from spreading into Renderer-facing code. The wrapper remains narrow rather than mirroring SDL.

### Static libraries only

Cost: M0 does not exercise DLL export rules or runtime deployment.

Reason accepted: shared-library ABI and plugin loading are not current requirements.

### No license selection in M0

Cost: the repository must not be represented as granting open-source usage rights until a license is added.

Reason accepted: license choice belongs to the repository owner and does not block local engineering work. A license must be selected before the first public release, as already stated in README.

## 15. Implementation Boundaries

The later implementation plan should divide M0 into reviewable checkpoints:

1. Root CMake project, presets, and dependency bootstrap
2. OwlFoundation logging, build information, and tests
3. OwlPlatform SDL lifecycle and window ownership
4. OwlSandbox command-line and visible smoke behavior
5. Warning policy, formatting, CI, and Windows documentation
6. Clean-clone acceptance and README status update

Each checkpoint must leave configure/build/test functional. M1 Vulkan design begins only after M0 satisfies its Definition of Done.
