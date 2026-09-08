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

## Local Vulkan Integration Tests

On a Vulkan 1.3-capable machine with an interactive desktop, opt in to the bootstrap tests:

```powershell
# Use the preset matching the installed Visual Studio version.
$env:OWL_RUN_VULKAN_BOOTSTRAP_TEST = "1"
ctest --preset windows-vs2026-debug -R "bootstrap locally" -V
Remove-Item Env:OWL_RUN_VULKAN_BOOTSTRAP_TEST
```

These tests briefly create SDL windows. One verifies Instance and Surface lifetime; the other also
selects a physical device, creates the logical Device and graphics/present queues, and checks move
construction, replacement, and destruction. They do not submit rendering work or present images.

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
