# Building OwlEngine on Windows

## Supported M0 Environment

- 64-bit Windows 10 or Windows 11
- Git
- CMake 3.28 or newer for Visual Studio 2022
- CMake 4.2 or newer for Visual Studio 2026
- Visual Studio 2022 17.14+ with v143, or Visual Studio 2026 18.0+ with v145
- The Desktop development with C++ workload for the selected Visual Studio version
- Network access during the first dependency bootstrap

OwlEngine uses a repository-local pinned vcpkg checkout. A global vcpkg installation and
`VCPKG_ROOT` are not required.

Both Visual Studio versions use the same `vcpkg.json`, registry baseline, and `.tools/vcpkg`
checkout. Their compiled dependencies remain separate under each CMake binary directory:
`build/windows-vs2022/vcpkg_installed` and `build/windows-vs2026/vcpkg_installed`.

## Configure

From the repository root, run only the preset matching the Visual Studio version installed on
the current machine:

```powershell
# Visual Studio 2022
./scripts/configure.ps1 -Preset windows-vs2022

# Visual Studio 2026
./scripts/configure.ps1 -Preset windows-vs2026
```

On first use, the script clones the pinned vcpkg revision into `.tools/vcpkg`, bootstraps the
tool, installs manifest dependencies into the selected build tree, and configures that tree.
Later runs reuse the managed checkout when its pin and executable are current.

## Build

```powershell
# Replace vs2026 with vs2022 on the VS2022 workstation.
cmake --build --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-relwithdebinfo
```

## Test

```powershell
# Replace vs2026 with vs2022 on the VS2022 workstation.
ctest --preset windows-vs2026-debug
ctest --preset windows-vs2026-relwithdebinfo
```

By default, CTest verifies CPU and command-line behavior and skips the local Vulkan integration
tests, so it does not open a GUI window.

Catch2 discovers tests at test time with a separate list per build configuration. This keeps
Debug and RelWithDebInfo executable paths independent when both configurations have been built.
Add `-N -V` to a CTest command to inspect its selected executable paths without running tests.

## Local Vulkan Integration Tests

On a Vulkan 1.3-capable machine with an interactive desktop, opt in to the bootstrap tests:

```powershell
# Use the preset matching the installed Visual Studio version.
$env:OWL_RUN_VULKAN_BOOTSTRAP_TEST = "1"
ctest --preset windows-vs2026-debug -R "^Vulkan .* locally$" -V
Remove-Item Env:OWL_RUN_VULKAN_BOOTSTRAP_TEST
```

These tests briefly create SDL windows and verify five scopes:

- Instance and Surface lifetime.
- Physical-device selection, Device and graphics/present queues, and move/replacement/destruction.
- Swapchain images/views, move/replacement, preflight rejection without resource loss, window-size
  changes, zero-extent deferral, and restoration without recreating device-level objects.
- Clear rendering: acquire, Dynamic Rendering attachment clear, Synchronization2 submission,
  present, rendered resize, minimized deferral, restoration, and explicit idle/cleanup. Each of
  the automatic presentation-fence and forced compatibility modes presents 300 frames.
- Indexed triangle rendering: the same frame/resize/minimize/restore and move/cleanup checks,
  with 300 presented frames per synchronization mode. Submitted draw counts are checked; this
  alone is not a pixel-output comparison. These tests read checked-in shaders from the source tree.

The resource-only swapchain test explicitly passes a zero extent because SDL can retain a
nonzero pixel size while minimized. The clear renderer also checks actual minimized/hidden
window state and suspends submission. Resize tests verify recreation and continued presentation;
they do not force a driver to return `VK_ERROR_OUT_OF_DATE_KHR`. Result-policy tests cover that
branch, but native failure injection remains pending. Replace `debug` with `relwithdebinfo` to
exercise that build configuration.

The renderer prefers optional `VK_KHR_swapchain_maintenance1`, then its EXT counterpart, when
their instance dependencies and device feature are available. These paths use presentation
fences for resource release. Without them, recreation/shutdown uses a conventional device-idle
fallback with a warning: core Vulkan idle waits do not strictly prove presentation-resource
release. The runtime log reports which path was enabled.

Debug and RelWithDebInfo request validation when `VK_LAYER_KHRONOS_validation` is available.
A missing layer produces a warning without failing the tests; such a pass is runtime evidence,
not evidence of a validation-clean run.

## Run the Visible Smoke Sample

```powershell
# Use build/windows-vs2022 on the VS2022 workstation.
./build/windows-vs2026/bin/Debug/OwlSandbox.exe --sample smoke
$LASTEXITCODE
```

The sample opens a resizable window. Press Escape or close the window; a successful run returns
exit code zero.

## Run the Visible Vulkan Clear Sample

```powershell
./build/windows-vs2026/bin/Debug/OwlSandbox.exe --sample clear
$LASTEXITCODE
```

This opens a persistent 1280x720 green clear window using the existing Vulkan frame lifecycle.
It requires a Vulkan 1.3-capable GPU and driver, but not a full Vulkan SDK installation. Validation
is requested when its layer is installed; a missing layer is reported and does not prevent running.

Check the following manually:

1. The client area is green, with no stale border after enlarging or maximizing the window.
2. Shrink, minimize, and restore the window; rendering resumes without a hang or crash.
3. Press Escape or close the window. The log reports frame counts and `Shutdown complete.`, and
   the exit code is zero. Exit code 3 indicates a Platform/window failure; 4 indicates a Vulkan failure.

The Sandbox owns event polling and sample pacing, including deferred frames while minimized.
It destroys the renderer before its borrowed Window, then shuts down Platform and logging.
The interactive loop is not a default CTest test; CPU tests cover parsing/help and an intentional
SDL initialization failure without opening a window.

On MSVC, Sandbox delay-loads `vulkan-1.dll`, preserving the non-Vulkan startup path for `--help`,
the default command, and `--sample smoke`. Its build uses vcpkg's PowerShell app-local deployer
because the pinned native deployer skips delay imports. Each configuration copies its matching
Vulkan Loader beside the executable; this does not install a GPU driver or a Validation Layer.

## Run the Indexed Triangle Sample

```powershell
./build/windows-vs2026/bin/Debug/OwlSandbox.exe --sample triangle
$LASTEXITCODE
```

Expected output is a centered RGB triangle over the green clear color. Check resizing, maximizing,
minimizing/restoring, and Escape/window-close exit as for Clear. The exit log also reports submitted
indexed draws. Missing or invalid Shader assets report the path and return code 4.

Shader paths resolve from the executable directory through Platform, not the current working
directory. Build copies both `.spv` files into `bin/<configuration>/assets/m1/` on every requested
Sandbox build, including asset-only updates and missing outputs. Keep that directory with the
executable when moving the runtime bundle. Smoke and Clear do not read these files.

The ordinary build requires no shader compiler. To edit shaders, see the adjacent
[shader sources and regeneration instructions](../../samples/owl_sandbox/assets/m1/README.md).
The first pipeline's [ownership and memory notes](../learning/m1-triangle.md) explain its deliberate
limits; general shader compilation, reflection, and resource allocation systems remain later work.

## Reconfigure from a Clean Build Directory

Resolve and validate the specific preset directory before deleting it:

```powershell
$preset = 'windows-vs2026' # Use windows-vs2022 on the VS2022 workstation.
if ($preset -notin @('windows-vs2022', 'windows-vs2026')) {
    throw "Unsupported preset: $preset"
}

$repositoryRoot = (Resolve-Path -LiteralPath '.').Path
$expectedBuildDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot "build/$preset"))
$buildDirectory = (Resolve-Path -LiteralPath $expectedBuildDirectory).Path

if (-not [string]::Equals(
    $buildDirectory,
    $expectedBuildDirectory,
    [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove unexpected path: $buildDirectory"
}

Remove-Item -LiteralPath $buildDirectory -Recurse -Force
./scripts/configure.ps1 -Preset $preset
```

Deleting one build tree also deletes only that toolchain's `vcpkg_installed` tree. It does not
delete the shared `.tools/vcpkg` checkout or the user-level vcpkg binary cache. Delete
`.tools/vcpkg` only when the bootstrap script reports that the managed checkout is dirty or
corrupt.

## Common Failures

- `cmake` not found or too old: install CMake 3.28+ for VS2022 or 4.2+ for VS2026, then reopen
  PowerShell.
- Visual Studio generator not found: install the Visual Studio version named by the preset and
  its Desktop development with C++ workload.
- vcpkg clone or package download fails: verify GitHub and network access, then rerun configure.
- Managed vcpkg checkout is dirty: remove only `.tools/vcpkg`, then rerun configure.
- CTest passes but no window was tested: run the visible smoke command manually.
