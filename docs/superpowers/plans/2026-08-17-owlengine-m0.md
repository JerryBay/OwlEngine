# OwlEngine M0 Engineering Baseline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the reproducible Win64 engineering baseline defined by the approved M0 design, ending with a tested SDL3 smoke window and documented clean-clone workflow.

**Architecture:** Create only three production targets: `OwlFoundation`, `OwlPlatform`, and `OwlSandbox`. Keep dependency direction one-way (`OwlSandbox -> OwlPlatform -> OwlFoundation`), hide spdlog and SDL3 from public headers, and obtain SDL3/spdlog/Catch2 through one repository-local pinned vcpkg checkout. Support VS2022/v143 and VS2026/v145 with explicit presets, separate CMake binary directories, and therefore separate manifest-mode dependency installation trees.

**Tech Stack:** C++20, CMake 3.28+ for VS2022 and 4.2+ for VS2026, Visual Studio 2022/v143, Visual Studio 2026/v145, PowerShell, vcpkg registry release 2026.07.29, SDL3, spdlog, Catch2, CTest, GitHub Actions.

**Design:** `docs/superpowers/specs/2026-08-17-owlengine-m0-design.md`

---

## File Map

### Root build and dependency files

- `.gitignore`: ignore repository-local tool and generated dependency directories.
- `.clang-format`: define the Owl-owned C++ formatting baseline.
- `CMakeLists.txt`: define the project, language contract, module order, and test entrypoints.
- `CMakePresets.json`: define VS2022 and VS2026 configure presets with isolated output plus Debug/RelWithDebInfo build/test presets for each.
- `vcpkg.json`: declare only SDL3, spdlog, and Catch2.
- `vcpkg-configuration.json`: pin the builtin registry and tool checkout to one official release commit.
- `cmake/OwlOptions.cmake`: own M0 project options.
- `cmake/OwlWarnings.cmake`: apply warnings only to Owl-owned targets.
- `scripts/bootstrap-vcpkg.ps1`: create and verify the ignored repository-local vcpkg checkout.
- `scripts/configure.ps1`: bootstrap dependencies and invoke the selected CMake preset.

### Foundation

- `engine/foundation/include/owl/foundation/BuildInfo.h`: public immutable build-information view.
- `engine/foundation/include/owl/foundation/CommandLine.h`: explicit M0 command parser contract.
- `engine/foundation/include/owl/foundation/Log.h`: Owl-owned logging API without spdlog types.
- `engine/foundation/include/owl/foundation/Assert.h`: invariant-reporting macro and function.
- `engine/foundation/src/BuildInfo.cpp`: return generated project/toolchain information.
- `engine/foundation/src/BuildInfoConfig.h.in`: CMake-generated string constants.
- `engine/foundation/src/CommandLine.cpp`: parse help and smoke commands.
- `engine/foundation/src/Log.cpp`: synchronized spdlog ownership.
- `engine/foundation/src/Assert.cpp`: log and abort failed invariants.
- `engine/foundation/CMakeLists.txt`: own Foundation sources, generated headers, and private dependencies.

### Platform

- `engine/platform/include/owl/platform/Window.h`: move-only window owner with no SDL types.
- `engine/platform/include/owl/platform/Platform.h`: SDL lifecycle, event pumping, and window factory.
- `engine/platform/src/sdl/Window.cpp`: SDL window storage and destruction.
- `engine/platform/src/sdl/Platform.cpp`: SDL initialization, shutdown, events, and version reporting.
- `engine/platform/CMakeLists.txt`: own Platform sources and private SDL3 link.

### Sandbox and tests

- `samples/owl_sandbox/src/main.cpp`: process entrypoint and exit-code mapping.
- `samples/owl_sandbox/src/SmokeSample.h`: internal smoke function declaration.
- `samples/owl_sandbox/src/SmokeSample.cpp`: visible smoke sample policy.
- `samples/owl_sandbox/CMakeLists.txt`: executable target, runtime output, and CLI CTest cases.
- `tests/foundation/BuildInfoTests.cpp`: build metadata tests.
- `tests/foundation/CommandLineTests.cpp`: command parser tests.
- `tests/foundation/LogTests.cpp`: repeated logging lifecycle tests.
- `tests/foundation/CMakeLists.txt`: Foundation Catch2 executable and discovery.
- `tests/platform/WindowTests.cpp`: null and move-state window tests without video initialization.
- `tests/platform/CMakeLists.txt`: Platform Catch2 executable and discovery.
- `tests/cmake/ExpectExitCode.cmake`: verify an executable returns an exact exit code.

### CI and documentation

- `.github/workflows/build-windows.yml`: VS2022 and VS2026 Windows configure/build/test matrix.
- `docs/building/windows.md`: prerequisites and supported local workflow.
- `README.md`: advertise build commands only after final acceptance.
- `docs/superpowers/specs/2026-08-17-owlengine-m0-design.md`: record M0 implementation completion only after all gates pass.

---

### Task 1: Reproducible CMake and vcpkg Bootstrap

**Files:**
- Modify: `.gitignore`
- Create: `vcpkg.json`
- Create: `vcpkg-configuration.json`
- Create: `scripts/bootstrap-vcpkg.ps1`
- Create: `scripts/configure.ps1`
- Create: `cmake/OwlOptions.cmake`
- Create: `cmake/OwlWarnings.cmake`
- Create: `CMakePresets.json`
- Create: `CMakeLists.txt`

- [ ] **Step 1: Verify the configure entrypoint is absent**

Run:

```powershell
cmake --preset windows-vs2022
cmake --preset windows-vs2026
```

Expected: both commands return nonzero with messages that the named configure presets do not exist.

- [ ] **Step 2: Extend `.gitignore` for the managed local tool checkout**

Add under the dependency-output section:

```gitignore
/.tools/
```

Do not ignore `vcpkg.json`, `vcpkg-configuration.json`, or `CMakePresets.json`.

- [ ] **Step 3: Add the pinned vcpkg manifest and registry configuration**

Create `vcpkg.json`:

```json
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "owlengine",
  "version-semver": "0.0.1",
  "dependencies": [
    "catch2",
    "sdl3",
    "spdlog"
  ]
}
```

Create `vcpkg-configuration.json`:

```json
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg-configuration.schema.json",
  "default-registry": {
    "kind": "builtin",
    "baseline": "9e593bb18ea69cc5095e012465dcd675a822ed0d"
  }
}
```

The hash is the signed official vcpkg `2026.07.29` registry release commit. The bootstrap script and registry use the same revision so there is one source of truth.

- [ ] **Step 4: Add the vcpkg bootstrap script**

Create `scripts/bootstrap-vcpkg.ps1`:

```powershell
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [Parameter(Mandatory = $true)]
        [string]$FailureMessage
    )

    & $Command @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$FailureMessage Exit code: $LASTEXITCODE"
    }
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$configurationPath = Join-Path $repositoryRoot 'vcpkg-configuration.json'
$configuration = Get-Content -LiteralPath $configurationPath -Raw | ConvertFrom-Json
$revision = [string]$configuration.'default-registry'.baseline

if ([string]::IsNullOrWhiteSpace($revision)) {
    throw "Missing default-registry.baseline in $configurationPath"
}

$toolsRoot = Join-Path $repositoryRoot '.tools'
$vcpkgRoot = Join-Path $toolsRoot 'vcpkg'
$vcpkgGitDirectory = Join-Path $vcpkgRoot '.git'

New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null

$isNewCheckout = -not (Test-Path -LiteralPath $vcpkgGitDirectory)
if ($isNewCheckout) {
    Invoke-NativeCommand `
        -Command 'git' `
        -Arguments @(
            'clone',
            '--filter=blob:none',
            '--no-checkout',
            'https://github.com/microsoft/vcpkg.git',
            $vcpkgRoot
        ) `
        -FailureMessage 'Failed to clone the vcpkg repository.'
}
else {
    $dirtyState = (& git -C $vcpkgRoot status --porcelain)
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to inspect the vcpkg checkout at $vcpkgRoot"
    }

    if ($dirtyState) {
        throw "The managed vcpkg checkout is dirty. Remove $vcpkgRoot and run configure again."
    }
}

$currentRevision = (& git -C $vcpkgRoot rev-parse HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or $currentRevision.Trim() -ne $revision) {
    Invoke-NativeCommand `
        -Command 'git' `
        -Arguments @('-C', $vcpkgRoot, 'fetch', '--depth', '1', 'origin', $revision) `
        -FailureMessage "Failed to fetch vcpkg revision $revision."

    Invoke-NativeCommand `
        -Command 'git' `
        -Arguments @('-C', $vcpkgRoot, 'checkout', '--detach', $revision) `
        -FailureMessage "Failed to check out vcpkg revision $revision."
}

$bootstrapScript = Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat'
& $bootstrapScript -disableMetrics | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "Failed to bootstrap vcpkg. Exit code: $LASTEXITCODE"
}

$vcpkgExecutable = Join-Path $vcpkgRoot 'vcpkg.exe'
if (-not (Test-Path -LiteralPath $vcpkgExecutable)) {
    throw "The vcpkg executable was not created at $vcpkgExecutable"
}

Write-Output $vcpkgRoot
```

- [ ] **Step 5: Add the supported configure wrapper**

Create `scripts/configure.ps1`:

```powershell
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('windows-vs2022', 'windows-vs2026')]
    [string]$Preset,

    [string[]]$CMakeArguments = @()
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$bootstrapScript = Join-Path $PSScriptRoot 'bootstrap-vcpkg.ps1'

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($null -eq $cmakeCommand) {
    throw 'CMake was not found on PATH.'
}

$cmakeVersionLine = (& cmake --version | Select-Object -First 1)
if ($LASTEXITCODE -ne 0 -or $cmakeVersionLine -notmatch '^cmake version (?<version>\d+\.\d+\.\d+)') {
    throw 'Unable to determine the installed CMake version.'
}

$cmakeVersion = [version]$Matches.version
$minimumCMakeVersion = if ($Preset -eq 'windows-vs2026') {
    [version]'4.2.0'
}
else {
    [version]'3.28.0'
}

if ($cmakeVersion -lt $minimumCMakeVersion) {
    throw "Preset $Preset requires CMake $minimumCMakeVersion or newer; found $cmakeVersion."
}

$vcpkgRoot = (& $bootstrapScript).Trim()
$toolchainFile = Join-Path $vcpkgRoot 'scripts/buildsystems/vcpkg.cmake'

if (-not (Test-Path -LiteralPath $toolchainFile)) {
    throw "The vcpkg toolchain file was not found at $toolchainFile"
}

$arguments = @(
    '--preset',
    $Preset,
    "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
) + $CMakeArguments

Push-Location $repositoryRoot
try {
    & cmake @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed. Exit code: $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
```

- [ ] **Step 6: Add project options and warning ownership**

Create `cmake/OwlOptions.cmake`:

```cmake
option(OWL_BUILD_TESTS "Build OwlEngine tests" ON)
option(OWL_WARNINGS_AS_ERRORS "Treat warnings in Owl-owned targets as errors" OFF)
```

Create `cmake/OwlWarnings.cmake`:

```cmake
add_library(OwlWarnings INTERFACE)

if(MSVC)
    target_compile_options(OwlWarnings INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /EHsc
    )

    if(OWL_WARNINGS_AS_ERRORS)
        target_compile_options(OwlWarnings INTERFACE /WX)
    endif()
else()
    target_compile_options(OwlWarnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )

    if(OWL_WARNINGS_AS_ERRORS)
        target_compile_options(OwlWarnings INTERFACE -Werror)
    endif()
endif()

function(owl_enable_warnings target_name)
    target_link_libraries(${target_name} PRIVATE OwlWarnings)
endfunction()
```

- [ ] **Step 7: Add the root project and CMake presets**

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)

project(OwlEngine VERSION 0.0.1 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(OwlOptions)
include(OwlWarnings)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(OWL_BUILD_TESTS)
    enable_testing()
endif()
```

Create `CMakePresets.json`:

```json
{
  "version": 6,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 28,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "windows-msvc-base",
      "hidden": true,
      "architecture": "x64",
      "cacheVariables": {
        "OWL_BUILD_TESTS": "ON",
        "VCPKG_TARGET_TRIPLET": "x64-windows"
      }
    },
    {
      "name": "windows-vs2022",
      "displayName": "Windows x64 - Visual Studio 2022",
      "inherits": "windows-msvc-base",
      "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build/windows-vs2022",
      "cacheVariables": {
        "CMAKE_INSTALL_PREFIX": "${sourceDir}/install/windows-vs2022",
        "VCPKG_INSTALLED_DIR": "${sourceDir}/build/windows-vs2022/vcpkg_installed"
      }
    },
    {
      "name": "windows-vs2026",
      "displayName": "Windows x64 - Visual Studio 2026",
      "inherits": "windows-msvc-base",
      "generator": "Visual Studio 18 2026",
      "binaryDir": "${sourceDir}/build/windows-vs2026",
      "cacheVariables": {
        "CMAKE_INSTALL_PREFIX": "${sourceDir}/install/windows-vs2026",
        "VCPKG_INSTALLED_DIR": "${sourceDir}/build/windows-vs2026/vcpkg_installed"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "windows-vs2022-debug",
      "configurePreset": "windows-vs2022",
      "configuration": "Debug"
    },
    {
      "name": "windows-vs2022-relwithdebinfo",
      "configurePreset": "windows-vs2022",
      "configuration": "RelWithDebInfo"
    },
    {
      "name": "windows-vs2026-debug",
      "configurePreset": "windows-vs2026",
      "configuration": "Debug"
    },
    {
      "name": "windows-vs2026-relwithdebinfo",
      "configurePreset": "windows-vs2026",
      "configuration": "RelWithDebInfo"
    }
  ],
  "testPresets": [
    {
      "name": "windows-vs2022-debug",
      "configurePreset": "windows-vs2022",
      "configuration": "Debug",
      "output": {
        "outputOnFailure": true
      }
    },
    {
      "name": "windows-vs2022-relwithdebinfo",
      "configurePreset": "windows-vs2022",
      "configuration": "RelWithDebInfo",
      "output": {
        "outputOnFailure": true
      }
    },
    {
      "name": "windows-vs2026-debug",
      "configurePreset": "windows-vs2026",
      "configuration": "Debug",
      "output": {
        "outputOnFailure": true
      }
    },
    {
      "name": "windows-vs2026-relwithdebinfo",
      "configurePreset": "windows-vs2026",
      "configuration": "RelWithDebInfo",
      "output": {
        "outputOnFailure": true
      }
    }
  ]
}
```

- [ ] **Step 8: Configure and verify the pinned toolchain**

Run:

```powershell
cmake --list-presets
./scripts/configure.ps1 -Preset windows-vs2026
git -C .tools/vcpkg rev-parse HEAD
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug
```

Expected:

- The preset list contains `windows-vs2022` and `windows-vs2026`.
- VS2026 configure succeeds and installs only the three declared ports plus their transitive build helpers into `build/windows-vs2026/vcpkg_installed`.
- `git rev-parse` prints `9e593bb18ea69cc5095e012465dcd675a822ed0d`.
- Build succeeds with no production targets yet.
- CTest reports that no tests were found and returns success.

The VS2022 preset uses the same manifest and pinned tool checkout but installs its v143-built dependencies under `build/windows-vs2022/vcpkg_installed`. Task 7 proves that second path on its matching CI image; Task 8 repeats it on the second developer machine.

- [ ] **Step 9: Commit the bootstrap baseline**

```powershell
git add .gitignore CMakeLists.txt CMakePresets.json vcpkg.json vcpkg-configuration.json cmake scripts
git commit -m "build: establish reproducible CMake baseline"
```

---

### Task 2: OwlFoundation Build Information

**Files:**
- Modify: `CMakeLists.txt`
- Create: `engine/foundation/CMakeLists.txt`
- Create: `engine/foundation/include/owl/foundation/BuildInfo.h`
- Create: `engine/foundation/src/BuildInfo.cpp`
- Create: `engine/foundation/src/BuildInfoConfig.h.in`
- Create: `tests/foundation/CMakeLists.txt`
- Create: `tests/foundation/BuildInfoTests.cpp`

- [ ] **Step 1: Write the build-information test first**

Create `tests/foundation/BuildInfoTests.cpp`:

```cpp
#include <owl/foundation/BuildInfo.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Build information contains the configured identity", "[build-info]")
{
    const owl::foundation::BuildInfo info = owl::foundation::GetBuildInfo();

    CHECK(info.projectVersion == "0.0.1");
    CHECK_FALSE(info.gitRevision.empty());
    CHECK_FALSE(info.buildConfiguration.empty());
    CHECK_FALSE(info.compiler.empty());
    CHECK(info.operatingSystem == "Windows");
    CHECK(info.architecture == "x64");
}
```

Create `tests/foundation/CMakeLists.txt`:

```cmake
add_executable(OwlFoundationTests)

target_sources(OwlFoundationTests PRIVATE
    BuildInfoTests.cpp
)

target_link_libraries(OwlFoundationTests PRIVATE
    Catch2::Catch2WithMain
    OwlFoundation
)

owl_enable_warnings(OwlFoundationTests)
catch_discover_tests(OwlFoundationTests)
```

- [ ] **Step 2: Wire the not-yet-implemented target and verify failure**

Replace the root `CMakeLists.txt` with:

```cmake
cmake_minimum_required(VERSION 3.28)

project(OwlEngine VERSION 0.0.1 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(OwlOptions)
include(OwlWarnings)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(engine/foundation)

if(OWL_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
    add_subdirectory(tests/foundation)
endif()
```

Run:

```powershell
./scripts/configure.ps1 -Preset windows-vs2026
```

Expected: configure fails because `engine/foundation/CMakeLists.txt` or the `OwlFoundation` target is not implemented yet.

- [ ] **Step 3: Add the public build-information contract**

Create `engine/foundation/include/owl/foundation/BuildInfo.h`:

```cpp
#pragma once

#include <string_view>

namespace owl::foundation
{
struct BuildInfo
{
    std::string_view projectVersion;
    std::string_view gitRevision;
    std::string_view buildConfiguration;
    std::string_view compiler;
    std::string_view operatingSystem;
    std::string_view architecture;
};

[[nodiscard]] BuildInfo GetBuildInfo() noexcept;
}
```

Create `engine/foundation/src/BuildInfoConfig.h.in`:

```cpp
#pragma once

#define OWL_PROJECT_VERSION "@PROJECT_VERSION@"
#define OWL_GIT_REVISION "@OWL_GIT_REVISION@"
#define OWL_COMPILER "@CMAKE_CXX_COMPILER_ID@ @CMAKE_CXX_COMPILER_VERSION@"
#define OWL_TARGET_SYSTEM "@CMAKE_SYSTEM_NAME@"
#define OWL_TARGET_ARCHITECTURE "@OWL_TARGET_ARCHITECTURE@"
```

Create `engine/foundation/src/BuildInfo.cpp`:

```cpp
#include <owl/foundation/BuildInfo.h>

#include <owl/foundation/generated/BuildInfoConfig.h>

namespace owl::foundation
{
BuildInfo GetBuildInfo() noexcept
{
    return BuildInfo{
        .projectVersion = OWL_PROJECT_VERSION,
        .gitRevision = OWL_GIT_REVISION,
        .buildConfiguration = OWL_BUILD_CONFIGURATION,
        .compiler = OWL_COMPILER,
        .operatingSystem = OWL_TARGET_SYSTEM,
        .architecture = OWL_TARGET_ARCHITECTURE,
    };
}
}
```

- [ ] **Step 4: Add the Foundation target and generated configuration**

Create `engine/foundation/CMakeLists.txt`:

```cmake
execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE OWL_GIT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE OWL_GIT_REVISION_RESULT
)

if(NOT OWL_GIT_REVISION_RESULT EQUAL 0 OR OWL_GIT_REVISION STREQUAL "")
    set(OWL_GIT_REVISION "unknown")
endif()

if(CMAKE_GENERATOR_PLATFORM)
    set(OWL_TARGET_ARCHITECTURE "${CMAKE_GENERATOR_PLATFORM}")
else()
    set(OWL_TARGET_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(OWL_FOUNDATION_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

configure_file(
    src/BuildInfoConfig.h.in
    "${OWL_FOUNDATION_GENERATED_DIR}/owl/foundation/generated/BuildInfoConfig.h"
    @ONLY
)

add_library(OwlFoundation STATIC)

target_sources(OwlFoundation PRIVATE
    src/BuildInfo.cpp
)

target_include_directories(OwlFoundation
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${OWL_FOUNDATION_GENERATED_DIR}"
)

target_compile_definitions(OwlFoundation PRIVATE
    OWL_BUILD_CONFIGURATION="$<CONFIG>"
)

owl_enable_warnings(OwlFoundation)
```

- [ ] **Step 5: Build and run the focused test**

```powershell
./scripts/configure.ps1 -Preset windows-vs2026
cmake --build --preset windows-vs2026-debug --target OwlFoundationTests
./build/windows-vs2026/tests/foundation/Debug/OwlFoundationTests.exe "[build-info]"
```

Expected: one Catch2 test case passes and reports no failed assertions.

- [ ] **Step 6: Commit build information**

```powershell
git add CMakeLists.txt engine/foundation tests/foundation
git commit -m "feat: add Foundation build information"
```

---

### Task 3: Command-Line Contract

**Files:**
- Create: `engine/foundation/include/owl/foundation/CommandLine.h`
- Create: `engine/foundation/src/CommandLine.cpp`
- Create: `tests/foundation/CommandLineTests.cpp`
- Modify: `engine/foundation/CMakeLists.txt`
- Modify: `tests/foundation/CMakeLists.txt`

- [ ] **Step 1: Add the parser contract and failing tests**

Create `engine/foundation/include/owl/foundation/CommandLine.h`:

```cpp
#pragma once

#include <span>
#include <string>
#include <string_view>

namespace owl::foundation
{
enum class Command
{
    Help,
    Smoke,
    Invalid,
};

struct ParsedCommandLine
{
    Command command = Command::Invalid;
    std::string error;
};

[[nodiscard]] ParsedCommandLine ParseCommandLine(
    std::span<const std::string_view> arguments);
}
```

Create `tests/foundation/CommandLineTests.cpp`:

```cpp
#include <owl/foundation/CommandLine.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <string_view>

using owl::foundation::Command;
using owl::foundation::ParseCommandLine;

TEST_CASE("No arguments selects help", "[command-line]")
{
    const auto parsed = ParseCommandLine(std::span<const std::string_view>{});
    CHECK(parsed.command == Command::Help);
    CHECK(parsed.error.empty());
}

TEST_CASE("Help option selects help", "[command-line]")
{
    constexpr std::array arguments{std::string_view{"--help"}};
    const auto parsed = ParseCommandLine(arguments);
    CHECK(parsed.command == Command::Help);
    CHECK(parsed.error.empty());
}

TEST_CASE("Smoke sample selects smoke", "[command-line]")
{
    constexpr std::array arguments{
        std::string_view{"--sample"},
        std::string_view{"smoke"},
    };
    const auto parsed = ParseCommandLine(arguments);
    CHECK(parsed.command == Command::Smoke);
    CHECK(parsed.error.empty());
}

TEST_CASE("Missing sample name is invalid", "[command-line]")
{
    constexpr std::array arguments{std::string_view{"--sample"}};
    const auto parsed = ParseCommandLine(arguments);
    CHECK(parsed.command == Command::Invalid);
    CHECK(parsed.error == "missing sample name after --sample");
}

TEST_CASE("Unknown arguments are invalid", "[command-line]")
{
    constexpr std::array arguments{std::string_view{"--unknown"}};
    const auto parsed = ParseCommandLine(arguments);
    CHECK(parsed.command == Command::Invalid);
    CHECK(parsed.error == "unknown command line");
}
```

Add `CommandLineTests.cpp` to `OwlFoundationTests` in `tests/foundation/CMakeLists.txt`:

```cmake
target_sources(OwlFoundationTests PRIVATE
    BuildInfoTests.cpp
    CommandLineTests.cpp
)
```

Run:

```powershell
./scripts/configure.ps1 -Preset windows-vs2026
cmake --build --preset windows-vs2026-debug --target OwlFoundationTests
```

Expected: link fails because `ParseCommandLine` is declared but not defined.

- [ ] **Step 2: Implement only the specified command grammar**

Create `engine/foundation/src/CommandLine.cpp`:

```cpp
#include <owl/foundation/CommandLine.h>

namespace owl::foundation
{
ParsedCommandLine ParseCommandLine(
    const std::span<const std::string_view> arguments)
{
    if (arguments.empty())
    {
        return ParsedCommandLine{.command = Command::Help};
    }

    if (arguments.size() == 1 && arguments[0] == "--help")
    {
        return ParsedCommandLine{.command = Command::Help};
    }

    if (arguments.size() == 1 && arguments[0] == "--sample")
    {
        return ParsedCommandLine{
            .command = Command::Invalid,
            .error = "missing sample name after --sample",
        };
    }

    if (arguments.size() == 2 && arguments[0] == "--sample" &&
        arguments[1] == "smoke")
    {
        return ParsedCommandLine{.command = Command::Smoke};
    }

    return ParsedCommandLine{
        .command = Command::Invalid,
        .error = "unknown command line",
    };
}
}
```

Add `src/CommandLine.cpp` to `OwlFoundation` in `engine/foundation/CMakeLists.txt`:

```cmake
target_sources(OwlFoundation PRIVATE
    src/BuildInfo.cpp
    src/CommandLine.cpp
)
```

- [ ] **Step 3: Run the focused parser tests and full Debug tests**

```powershell
cmake --build --preset windows-vs2026-debug --target OwlFoundationTests
./build/windows-vs2026/tests/foundation/Debug/OwlFoundationTests.exe "[command-line]"
ctest --preset windows-vs2026-debug
```

Expected: five parser cases pass; the complete Debug CTest preset reports zero failures.

- [ ] **Step 4: Commit the command-line contract**

```powershell
git add engine/foundation tests/foundation
git commit -m "feat: add Sandbox command parsing"
```

---

### Task 4: Logging and Assertions

**Files:**
- Create: `engine/foundation/include/owl/foundation/Log.h`
- Create: `engine/foundation/include/owl/foundation/Assert.h`
- Create: `engine/foundation/src/Log.cpp`
- Create: `engine/foundation/src/Assert.cpp`
- Create: `tests/foundation/LogTests.cpp`
- Modify: `engine/foundation/CMakeLists.txt`
- Modify: `tests/foundation/CMakeLists.txt`

- [ ] **Step 1: Add the logging contract and failing lifecycle test**

Create `engine/foundation/include/owl/foundation/Log.h`:

```cpp
#pragma once

#include <string_view>

namespace owl::foundation
{
enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
};

void InitializeLogging();
void ShutdownLogging();
void SetLogLevel(LogLevel level);
void LogMessage(
    LogLevel level,
    std::string_view category,
    std::string_view message);
}
```

Create `tests/foundation/LogTests.cpp`:

```cpp
#include <owl/foundation/Log.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Logging initialization and shutdown are repeatable", "[logging]")
{
    using namespace owl::foundation;

    InitializeLogging();
    InitializeLogging();
    SetLogLevel(LogLevel::Trace);
    LogMessage(LogLevel::Info, "Test", "logging lifecycle");
    ShutdownLogging();
    ShutdownLogging();

    SUCCEED();
}
```

Add `LogTests.cpp` to `tests/foundation/CMakeLists.txt`:

```cmake
target_sources(OwlFoundationTests PRIVATE
    BuildInfoTests.cpp
    CommandLineTests.cpp
    LogTests.cpp
)
```

Run the Foundation test build. Expected: unresolved logging symbols.

- [ ] **Step 2: Implement synchronized logging without leaking spdlog types**

Create `engine/foundation/src/Log.cpp`:

```cpp
#include <owl/foundation/Log.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <string>

namespace owl::foundation
{
namespace
{
std::mutex loggerMutex;
std::shared_ptr<spdlog::logger> logger;

spdlog::level::level_enum ToSpdlogLevel(const LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warning:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    case LogLevel::Critical:
        return spdlog::level::critical;
    }

    return spdlog::level::info;
}

std::shared_ptr<spdlog::logger> GetOrCreateLogger()
{
    std::scoped_lock lock(loggerMutex);
    if (!logger)
    {
        logger = spdlog::get("Owl");
        if (!logger)
        {
            logger = spdlog::stdout_color_mt("Owl");
        }
        logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    }
    return logger;
}
}

void InitializeLogging()
{
    static_cast<void>(GetOrCreateLogger());
}

void ShutdownLogging()
{
    std::scoped_lock lock(loggerMutex);
    if (logger)
    {
        logger->flush();
        logger.reset();
        spdlog::drop("Owl");
    }
}

void SetLogLevel(const LogLevel level)
{
    GetOrCreateLogger()->set_level(ToSpdlogLevel(level));
}

void LogMessage(
    const LogLevel level,
    const std::string_view category,
    const std::string_view message)
{
    const std::string formatted =
        "[" + std::string(category) + "] " + std::string(message);
    GetOrCreateLogger()->log(ToSpdlogLevel(level), formatted);
}
}
```

- [ ] **Step 3: Add the development assertion contract**

Create `engine/foundation/include/owl/foundation/Assert.h`:

```cpp
#pragma once

#include <string_view>

namespace owl::foundation
{
[[noreturn]] void ReportAssertion(
    std::string_view expression,
    std::string_view message,
    std::string_view file,
    int line);
}

#if defined(OWL_ENABLE_ASSERTS)
#define OWL_ASSERT(expression, message)                                      \
    ((expression)                                                            \
         ? static_cast<void>(0)                                              \
         : ::owl::foundation::ReportAssertion(                               \
               #expression, (message), __FILE__, __LINE__))
#else
#define OWL_ASSERT(expression, message)                                      \
    do                                                                       \
    {                                                                        \
        static_cast<void>(sizeof(expression));                               \
        static_cast<void>(sizeof(message));                                  \
    } while (false)
#endif
```

Create `engine/foundation/src/Assert.cpp`:

```cpp
#include <owl/foundation/Assert.h>

#include <owl/foundation/Log.h>

#include <cstdlib>
#include <string>

namespace owl::foundation
{
[[noreturn]] void ReportAssertion(
    const std::string_view expression,
    const std::string_view message,
    const std::string_view file,
    const int line)
{
    const std::string diagnostic =
        std::string(expression) + " | " + std::string(message) + " | " +
        std::string(file) + ":" + std::to_string(line);
    LogMessage(LogLevel::Critical, "Assert", diagnostic);
    std::abort();
}
}
```

- [ ] **Step 4: Link spdlog privately and enable assertions for development configurations**

Update `engine/foundation/CMakeLists.txt` so the target section is:

```cmake
find_package(spdlog CONFIG REQUIRED)

add_library(OwlFoundation STATIC)

target_sources(OwlFoundation PRIVATE
    src/Assert.cpp
    src/BuildInfo.cpp
    src/CommandLine.cpp
    src/Log.cpp
)

target_include_directories(OwlFoundation
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${OWL_FOUNDATION_GENERATED_DIR}"
)

target_compile_definitions(OwlFoundation
    PUBLIC
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:OWL_ENABLE_ASSERTS=1>
    PRIVATE
        OWL_BUILD_CONFIGURATION="$<CONFIG>"
)

target_link_libraries(OwlFoundation PRIVATE spdlog::spdlog)

owl_enable_warnings(OwlFoundation)
```

Keep the Git revision and `configure_file` block above this target section unchanged.

- [ ] **Step 5: Run logging and full Foundation tests**

```powershell
./scripts/configure.ps1 -Preset windows-vs2026
cmake --build --preset windows-vs2026-debug --target OwlFoundationTests
./build/windows-vs2026/tests/foundation/Debug/OwlFoundationTests.exe "[logging]"
ctest --preset windows-vs2026-debug
```

Expected: logging lifecycle test passes; all Foundation CTest cases pass.

- [ ] **Step 6: Commit Foundation diagnostics**

```powershell
git add engine/foundation tests/foundation
git commit -m "feat: add Foundation diagnostics"
```

---

### Task 5: SDL3 Platform Lifecycle and Window Ownership

**Files:**
- Modify: `CMakeLists.txt`
- Create: `engine/platform/include/owl/platform/Window.h`
- Create: `engine/platform/include/owl/platform/Platform.h`
- Create: `engine/platform/src/sdl/Window.cpp`
- Create: `engine/platform/src/sdl/Platform.cpp`
- Create: `engine/platform/CMakeLists.txt`
- Create: `tests/platform/WindowTests.cpp`
- Create: `tests/platform/CMakeLists.txt`

- [ ] **Step 1: Add the move-only Window contract and failing tests**

Create `engine/platform/include/owl/platform/Window.h`:

```cpp
#pragma once

#include <memory>

namespace owl::platform
{
class Platform;

class Window
{
public:
    Window() noexcept;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;

private:
    friend class Platform;

    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};
}
```

Create `tests/platform/WindowTests.cpp`:

```cpp
#include <owl/platform/Window.h>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<owl::platform::Window>);
static_assert(!std::is_copy_assignable_v<owl::platform::Window>);
static_assert(std::is_move_constructible_v<owl::platform::Window>);
static_assert(std::is_move_assignable_v<owl::platform::Window>);

TEST_CASE("Default and moved empty windows remain invalid", "[window]")
{
    owl::platform::Window source;
    CHECK_FALSE(source.IsValid());

    owl::platform::Window destination{std::move(source)};
    CHECK_FALSE(source.IsValid());
    CHECK_FALSE(destination.IsValid());
}
```

Create `tests/platform/CMakeLists.txt`:

```cmake
add_executable(OwlPlatformTests)

target_sources(OwlPlatformTests PRIVATE
    WindowTests.cpp
)

target_link_libraries(OwlPlatformTests PRIVATE
    Catch2::Catch2WithMain
    OwlPlatform
)

owl_enable_warnings(OwlPlatformTests)
catch_discover_tests(OwlPlatformTests)
```

Update root `CMakeLists.txt` by adding `add_subdirectory(engine/platform)` after Foundation and `add_subdirectory(tests/platform)` after Foundation tests.

Run configure. Expected: failure because `OwlPlatform` has not been implemented.

- [ ] **Step 2: Add the Platform lifecycle contract**

Create `engine/platform/include/owl/platform/Platform.h`:

```cpp
#pragma once

#include <owl/platform/Window.h>

#include <optional>
#include <string>

namespace owl::platform
{
struct WindowDesc
{
    std::string title;
    int width = 1280;
    int height = 720;
    bool resizable = true;
};

enum class EventPumpResult
{
    Continue,
    Quit,
};

class Platform
{
public:
    Platform() noexcept;
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    Platform(Platform&& other) noexcept;
    Platform& operator=(Platform&& other) noexcept;

    [[nodiscard]] static std::optional<Platform> Create(std::string& error);

    [[nodiscard]] std::optional<Window> CreateWindow(
        const WindowDesc& desc,
        std::string& error) const;

    [[nodiscard]] EventPumpResult PumpEvents() const noexcept;
    [[nodiscard]] std::string SdlVersion() const;
    [[nodiscard]] std::string VideoDriver() const;

private:
    explicit Platform(bool initialized) noexcept;

    bool initialized_ = false;
};
}
```

- [ ] **Step 3: Implement SDL initialization and event pumping**

Create `engine/platform/src/sdl/Platform.cpp`:

```cpp
#include <owl/foundation/Assert.h>
#include <owl/platform/Platform.h>

#include <SDL3/SDL.h>

#include <string>
#include <utility>

namespace owl::platform
{
Platform::Platform() noexcept = default;

Platform::Platform(const bool initialized) noexcept
    : initialized_(initialized)
{
}

Platform::~Platform()
{
    if (initialized_)
    {
        SDL_Quit();
    }
}

Platform::Platform(Platform&& other) noexcept
    : initialized_(std::exchange(other.initialized_, false))
{
}

Platform& Platform::operator=(Platform&& other) noexcept
{
    if (this != &other)
    {
        if (initialized_)
        {
            SDL_Quit();
        }
        initialized_ = std::exchange(other.initialized_, false);
    }
    return *this;
}

std::optional<Platform> Platform::Create(std::string& error)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        error = std::string{"SDL_Init failed: "} + SDL_GetError();
        return std::nullopt;
    }

    error.clear();
    return Platform{true};
}

EventPumpResult Platform::PumpEvents() const noexcept
{
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT ||
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            return EventPumpResult::Quit;
        }

        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
        {
            return EventPumpResult::Quit;
        }
    }

    return EventPumpResult::Continue;
}

std::string Platform::SdlVersion() const
{
    return std::to_string(SDL_MAJOR_VERSION) + "." +
        std::to_string(SDL_MINOR_VERSION) + "." +
        std::to_string(SDL_MICRO_VERSION);
}

std::string Platform::VideoDriver() const
{
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver != nullptr ? std::string{driver} : std::string{"unknown"};
}
}
```

- [ ] **Step 4: Implement private SDL window storage**

Create `engine/platform/src/sdl/Window.cpp`:

```cpp
#include <owl/platform/Platform.h>
#include <owl/platform/Window.h>

#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace owl::platform
{
struct Window::Impl
{
    explicit Impl(SDL_Window* window) noexcept
        : window(window)
    {
    }

    ~Impl()
    {
        if (window != nullptr)
        {
            SDL_DestroyWindow(window);
        }
    }

    SDL_Window* window = nullptr;
};

Window::Window() noexcept = default;
Window::~Window() = default;
Window::Window(Window&& other) noexcept = default;
Window& Window::operator=(Window&& other) noexcept = default;

Window::Window(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

bool Window::IsValid() const noexcept
{
    return impl_ != nullptr && impl_->window != nullptr;
}

std::optional<Window> Platform::CreateWindow(
    const WindowDesc& desc,
    std::string& error) const
{
    if (!initialized_)
    {
        error = "Platform is not initialized";
        return std::nullopt;
    }

    OWL_ASSERT(desc.width > 0, "Window width must be positive");
    OWL_ASSERT(desc.height > 0, "Window height must be positive");

    SDL_WindowFlags flags = 0;
    if (desc.resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    SDL_Window* handle = SDL_CreateWindow(
        desc.title.c_str(),
        desc.width,
        desc.height,
        flags);

    if (handle == nullptr)
    {
        error = std::string{"SDL_CreateWindow failed: "} + SDL_GetError();
        return std::nullopt;
    }

    error.clear();
    return Window{std::make_unique<Window::Impl>(handle)};
}
}
```

- [ ] **Step 5: Add the Platform target with private SDL ownership**

Create `engine/platform/CMakeLists.txt`:

```cmake
find_package(SDL3 CONFIG REQUIRED)

add_library(OwlPlatform STATIC)

target_sources(OwlPlatform PRIVATE
    src/sdl/Platform.cpp
    src/sdl/Window.cpp
)

target_include_directories(OwlPlatform PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(OwlPlatform
    PRIVATE
        OwlFoundation
        SDL3::SDL3
)

owl_enable_warnings(OwlPlatform)
```

- [ ] **Step 6: Build and run tests without creating a visible window**

```powershell
./scripts/configure.ps1 -Preset windows-vs2026
cmake --build --preset windows-vs2026-debug --target OwlPlatformTests
./build/windows-vs2026/tests/platform/Debug/OwlPlatformTests.exe "[window]"
ctest --preset windows-vs2026-debug
```

Expected: Window ownership test passes; no SDL video window opens; full CTest reports zero failures.

- [ ] **Step 7: Commit the Platform boundary**

```powershell
git add CMakeLists.txt engine/platform tests/platform
git commit -m "feat: add SDL3 Platform boundary"
```

---

### Task 6: OwlSandbox CLI and Visible Smoke Sample

**Files:**
- Modify: `CMakeLists.txt`
- Create: `samples/owl_sandbox/CMakeLists.txt`
- Create: `samples/owl_sandbox/src/main.cpp`
- Create: `samples/owl_sandbox/src/SmokeSample.h`
- Create: `samples/owl_sandbox/src/SmokeSample.cpp`
- Create: `tests/cmake/ExpectExitCode.cmake`

- [ ] **Step 1: Add executable tests before the executable implementation**

Create `tests/cmake/ExpectExitCode.cmake`:

```cmake
if(NOT DEFINED PROGRAM)
    message(FATAL_ERROR "PROGRAM is required")
endif()

if(NOT DEFINED EXPECTED_EXIT_CODE)
    message(FATAL_ERROR "EXPECTED_EXIT_CODE is required")
endif()

if(NOT DEFINED PROGRAM_ARGUMENT)
    message(FATAL_ERROR "PROGRAM_ARGUMENT is required")
endif()

execute_process(
    COMMAND "${PROGRAM}" "${PROGRAM_ARGUMENT}"
    RESULT_VARIABLE actual_exit_code
)

if(NOT "${actual_exit_code}" STREQUAL "${EXPECTED_EXIT_CODE}")
    message(FATAL_ERROR
        "Expected exit code ${EXPECTED_EXIT_CODE}, got ${actual_exit_code}")
endif()
```

Create `samples/owl_sandbox/CMakeLists.txt`:

```cmake
add_executable(OwlSandbox)

target_sources(OwlSandbox PRIVATE
    src/main.cpp
    src/SmokeSample.cpp
)

target_link_libraries(OwlSandbox PRIVATE
    OwlFoundation
    OwlPlatform
)

set_target_properties(OwlSandbox PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

owl_enable_warnings(OwlSandbox)

if(OWL_BUILD_TESTS)
    add_test(
        NAME OwlSandbox.Help
        COMMAND $<TARGET_FILE:OwlSandbox> --help
    )

    add_test(
        NAME OwlSandbox.InvalidArguments
        COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:OwlSandbox>
            -DEXPECTED_EXIT_CODE=2
            -DPROGRAM_ARGUMENT=--unknown
            -P ${PROJECT_SOURCE_DIR}/tests/cmake/ExpectExitCode.cmake
    )
endif()
```

Add `add_subdirectory(samples/owl_sandbox)` after Platform in the root `CMakeLists.txt`.

Run configure. Expected: failure because `main.cpp` and `SmokeSample.cpp` do not exist.

- [ ] **Step 2: Add the internal smoke declaration**

Create `samples/owl_sandbox/src/SmokeSample.h`:

```cpp
#pragma once

namespace owl::sandbox
{
[[nodiscard]] int RunSmokeSample();
}
```

- [ ] **Step 3: Implement CLI dispatch and stable exit codes**

Create `samples/owl_sandbox/src/main.cpp`:

```cpp
#include "SmokeSample.h"

#include <owl/foundation/CommandLine.h>

#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace
{
void PrintHelp()
{
    std::cout
        << "OwlSandbox\n"
        << "  --help          Show this help\n"
        << "  --sample smoke  Run the M0 SDL3 smoke sample\n";
}
}

int main(const int argc, char* argv[])
{
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index)
    {
        arguments.emplace_back(argv[index]);
    }

    const owl::foundation::ParsedCommandLine parsed =
        owl::foundation::ParseCommandLine(
            std::span<const std::string_view>{arguments});

    switch (parsed.command)
    {
    case owl::foundation::Command::Help:
        PrintHelp();
        return 0;
    case owl::foundation::Command::Smoke:
        return owl::sandbox::RunSmokeSample();
    case owl::foundation::Command::Invalid:
        std::cerr << "Error: " << parsed.error << "\n"
                  << "Run OwlSandbox --help for usage.\n";
        return 2;
    }

    return 2;
}
```

- [ ] **Step 4: Implement the visible smoke behavior**

Create `samples/owl_sandbox/src/SmokeSample.cpp`:

```cpp
#include "SmokeSample.h"

#include <owl/foundation/BuildInfo.h>
#include <owl/foundation/Log.h>
#include <owl/platform/Platform.h>

#include <chrono>
#include <optional>
#include <string>
#include <thread>

namespace owl::sandbox
{
namespace
{
void LogBuildInformation()
{
    using owl::foundation::LogLevel;
    using owl::foundation::LogMessage;

    const owl::foundation::BuildInfo info = owl::foundation::GetBuildInfo();
    LogMessage(LogLevel::Info, "Build", "Version: " + std::string(info.projectVersion));
    LogMessage(LogLevel::Info, "Build", "Revision: " + std::string(info.gitRevision));
    LogMessage(LogLevel::Info, "Build", "Configuration: " + std::string(info.buildConfiguration));
    LogMessage(LogLevel::Info, "Build", "Compiler: " + std::string(info.compiler));
    LogMessage(LogLevel::Info, "Build", "OS: " + std::string(info.operatingSystem));
    LogMessage(LogLevel::Info, "Build", "Architecture: " + std::string(info.architecture));
}
}

int RunSmokeSample()
{
    using owl::foundation::LogLevel;
    using owl::foundation::LogMessage;

    owl::foundation::InitializeLogging();
    LogBuildInformation();

    std::string error;
    std::optional<owl::platform::Platform> platform =
        owl::platform::Platform::Create(error);
    if (!platform)
    {
        LogMessage(LogLevel::Error, "Platform", error);
        owl::foundation::ShutdownLogging();
        return 3;
    }

    LogMessage(LogLevel::Info, "Platform", "SDL: " + platform->SdlVersion());
    LogMessage(LogLevel::Info, "Platform", "Video driver: " + platform->VideoDriver());

    owl::platform::WindowDesc windowDesc{
        .title = "OwlEngine - M0 Smoke",
        .width = 1280,
        .height = 720,
        .resizable = true,
    };

    std::optional<owl::platform::Window> window =
        platform->CreateWindow(windowDesc, error);
    if (!window)
    {
        LogMessage(LogLevel::Error, "Platform", error);
        owl::foundation::ShutdownLogging();
        return 3;
    }

    LogMessage(LogLevel::Info, "Smoke", "Window created; press Escape or close the window.");

    while (platform->PumpEvents() == owl::platform::EventPumpResult::Continue)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    window.reset();
    platform.reset();
    LogMessage(LogLevel::Info, "Smoke", "Shutdown complete.");
    owl::foundation::ShutdownLogging();
    return 0;
}
}
```

- [ ] **Step 5: Build and run the automated CLI tests**

```powershell
./scripts/configure.ps1 -Preset windows-vs2026
cmake --build --preset windows-vs2026-debug --target OwlSandbox
ctest --preset windows-vs2026-debug -R OwlSandbox
```

Expected: `OwlSandbox.Help` and `OwlSandbox.InvalidArguments` both pass; no window opens during CTest.

- [ ] **Step 6: Run the visible smoke test through both shutdown paths**

Run once and press Escape:

```powershell
./build/windows-vs2026/bin/Debug/OwlSandbox.exe --sample smoke
$LASTEXITCODE
```

Run again and use the window close button:

```powershell
./build/windows-vs2026/bin/Debug/OwlSandbox.exe --sample smoke
$LASTEXITCODE
```

Expected for each run:

- A responsive 1280 x 720 resizable window titled `OwlEngine - M0 Smoke`.
- Build, compiler, platform, architecture, and SDL version log lines.
- Exit code `0` after the selected shutdown path.

- [ ] **Step 7: Run full Debug verification and commit**

```powershell
ctest --preset windows-vs2026-debug
git add CMakeLists.txt samples tests/cmake
git commit -m "feat: add M0 smoke sample"
```

Expected: all Debug tests pass before the commit is created.

---

### Task 7: Formatting, CI, and Windows Build Documentation

**Files:**
- Create: `.clang-format`
- Create: `.github/workflows/build-windows.yml`
- Create: `docs/building/windows.md`

- [ ] **Step 1: Add the formatting baseline**

Create `.clang-format`:

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
BreakBeforeBraces: Allman
AllowShortFunctionsOnASingleLine: Empty
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
PointerAlignment: Left
SortIncludes: CaseSensitive
SpaceBeforeParens: ControlStatements
```

- [ ] **Step 2: Format Owl-owned C++ sources and confirm no semantic diff**

Run:

```powershell
$sourceFiles = rg --files engine samples tests -g '*.h' -g '*.cpp'
clang-format -i $sourceFiles
git diff --check
```

Expected: formatting completes; `git diff --check` returns success. Review `git diff` to confirm only formatting changes occurred.

- [ ] **Step 3: Add the dual-toolchain Windows CI workflow**

Create `.github/workflows/build-windows.yml`:

```yaml
name: Windows Build

on:
  push:
    branches:
      - master
  pull_request:

permissions:
  contents: read

jobs:
  build:
    name: ${{ matrix.name }}
    runs-on: ${{ matrix.runner }}

    strategy:
      fail-fast: false
      matrix:
        include:
          - name: Visual Studio 2022
            runner: windows-2022
            configurePreset: windows-vs2022
            debugPreset: windows-vs2022-debug
            relWithDebInfoPreset: windows-vs2022-relwithdebinfo
          - name: Visual Studio 2026
            runner: windows-2025-vs2026
            configurePreset: windows-vs2026
            debugPreset: windows-vs2026-debug
            relWithDebInfoPreset: windows-vs2026-relwithdebinfo

    steps:
      - name: Checkout
        uses: actions/checkout@v6

      - name: Configure
        shell: pwsh
        run: >-
          ./scripts/configure.ps1
          -Preset '${{ matrix.configurePreset }}'
          -CMakeArguments '-DOWL_WARNINGS_AS_ERRORS=ON'

      - name: Build Debug
        shell: pwsh
        run: cmake --build --preset '${{ matrix.debugPreset }}'

      - name: Test Debug
        shell: pwsh
        run: ctest --preset '${{ matrix.debugPreset }}'

      - name: Build RelWithDebInfo
        shell: pwsh
        run: cmake --build --preset '${{ matrix.relWithDebInfoPreset }}'

      - name: Test RelWithDebInfo
        shell: pwsh
        run: ctest --preset '${{ matrix.relWithDebInfoPreset }}'
```

Use explicit runner labels. `windows-2022` supplies VS2022/v143 and `windows-2025-vs2026` supplies VS2026/v145. Do not use `windows-latest`; a moving label would weaken the toolchain contract.

- [ ] **Step 4: Add exact Windows prerequisites and commands**

Create `docs/building/windows.md`:

````markdown
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

From the repository root:

```powershell
# Visual Studio 2022
./scripts/configure.ps1 -Preset windows-vs2022

# Visual Studio 2026
./scripts/configure.ps1 -Preset windows-vs2026
```

The script clones the pinned vcpkg revision into `.tools/vcpkg`, bootstraps vcpkg, installs
the manifest dependencies into the selected build tree, and configures that tree. Run only
the preset matching the Visual Studio version installed on the current machine.

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

CTest verifies CPU behavior and command-line behavior. It does not open a GUI window.

## Run the Visible Smoke Sample

```powershell
# Use build/windows-vs2022 on the VS2022 workstation.
./build/windows-vs2026/bin/Debug/OwlSandbox.exe --sample smoke
```

The sample opens a resizable window. Press Escape or close the window; a successful run
returns exit code zero.

## Reconfigure from a Clean Build Directory

Resolve the intended build directory before deleting it:

```powershell
$preset = 'windows-vs2026' # Use windows-vs2022 on the VS2022 workstation.
$buildDirectory = Resolve-Path -LiteralPath "./build/$preset"
$repositoryRoot = Resolve-Path -LiteralPath '.'
if ($buildDirectory.Path.StartsWith($repositoryRoot.Path)) {
    Remove-Item -LiteralPath $buildDirectory.Path -Recurse -Force
}
./scripts/configure.ps1 -Preset $preset
```

Deleting one build tree also deletes only that toolchain's `vcpkg_installed` tree. It does
not delete the shared `.tools/vcpkg` checkout or the user-level vcpkg binary cache. Delete
`.tools/vcpkg` only when the bootstrap script reports that the managed checkout is dirty or
corrupt.

## Common Failures

- `cmake` not found or too old: install CMake 3.28+ for VS2022 or 4.2+ for VS2026, then
  reopen PowerShell.
- Visual Studio generator not found: install the Visual Studio version named by the preset
  and its Desktop development with C++ workload.
- vcpkg clone or package download fails: verify GitHub/network access, then rerun configure.
- managed vcpkg checkout is dirty: remove only `.tools/vcpkg`, then rerun configure.
- CTest passes but no window was tested: run the visible smoke command manually.
````

- [ ] **Step 5: Run both VS2026 local configurations with warnings as errors**

```powershell
./scripts/configure.ps1 -Preset windows-vs2026 -CMakeArguments '-DOWL_WARNINGS_AS_ERRORS=ON'
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-relwithdebinfo
ctest --preset windows-vs2026-relwithdebinfo
```

Expected: both VS2026 builds succeed and both CTest presets report zero failures. Task 8 publishes this commit and requires equivalent VS2022 CI evidence before M0 can be accepted.

- [ ] **Step 6: Commit CI and documentation**

```powershell
git add .clang-format .github docs/building engine samples tests
git commit -m "ci: verify M0 Windows builds"
```

---

### Task 8: Clean-Clone Acceptance and Public Status

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-08-17-owlengine-m0-design.md`

- [ ] **Step 1: Verify the current working tree before clean-clone testing**

```powershell
git status --short
```

Expected: no output. Do not create the acceptance clone from an uncommitted workspace.

- [ ] **Step 2: Clone into a unique temporary directory**

```powershell
$cleanRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("OwlEngine-m0-" + [guid]::NewGuid())
git clone . $cleanRoot
Set-Location $cleanRoot
```

Expected: clone succeeds into a newly generated directory, so no existing path is deleted or overwritten.

- [ ] **Step 3: Repeat the complete clean-clone workflow**

```powershell
./scripts/configure.ps1 -Preset windows-vs2026
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-relwithdebinfo
ctest --preset windows-vs2026-relwithdebinfo
./build/windows-vs2026/bin/Debug/OwlSandbox.exe --sample smoke
$LASTEXITCODE
```

Expected:

- Both configurations build.
- Both CTest presets report zero failures.
- The visible smoke window is responsive and exits through Escape or the close button.
- Final exit code is `0`.
- No source file or machine-specific path is edited.

- [ ] **Step 4: Return to the source checkout**

```powershell
Set-Location E:/UnityProject/OwlEngine
git status --short
```

Expected: no output.

- [ ] **Step 5: Publish the implementation commit and inspect dual-toolchain CI**

Request explicit authorization before pushing. After authorization:

```powershell
git push origin master
```

Inspect the `Windows Build` workflow and require both toolchain jobs and all ten configure/build/test steps to pass from a cache miss. The push also makes the exact implementation commit available for clean-clone testing on the VS2022 workstation. A local build is not evidence that GitHub Actions passed.

- [ ] **Step 6: Repeat clean-clone acceptance on the VS2022 workstation**

From an existing OwlEngine checkout on the VS2022 machine, run:

```powershell
$sourceRepository = git remote get-url origin
$cleanRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("OwlEngine-m0-vs2022-" + [guid]::NewGuid())
git clone $sourceRepository $cleanRoot
Set-Location $cleanRoot
./scripts/configure.ps1 -Preset windows-vs2022
cmake --build --preset windows-vs2022-debug
ctest --preset windows-vs2022-debug
cmake --build --preset windows-vs2022-relwithdebinfo
ctest --preset windows-vs2022-relwithdebinfo
./build/windows-vs2022/bin/Debug/OwlSandbox.exe --sample smoke
$LASTEXITCODE
```

Expected: the same clean-clone, two-configuration, zero-test-failure, responsive-window, and zero-exit-code evidence as VS2026. Confirm that dependencies were installed under `build/windows-vs2022/vcpkg_installed`; no v145 binary is copied into this tree.

- [ ] **Step 7: Update README only after both local acceptances succeed**

Replace the two paragraphs under `## Project Status` with:

```markdown
OwlEngine M0 is implemented. The repository provides a reproducible Win64 configure,
build, test, and SDL3 smoke-sample workflow using Visual Studio 2022/v143 or Visual Studio
2026/v145, CMake Presets, and one repository-local pinned vcpkg checkout.

See [Building OwlEngine on Windows](docs/building/windows.md) for prerequisites and exact
commands.
```

Add a `## Quick Start` section immediately after Project Status:

````markdown
## Quick Start

```powershell
$preset = 'windows-vs2026' # Use windows-vs2022 on the VS2022 workstation.
./scripts/configure.ps1 -Preset $preset
cmake --build --preset "$preset-debug"
ctest --preset "$preset-debug"
& "./build/$preset/bin/Debug/OwlSandbox.exe" --sample smoke
```
````

- [ ] **Step 8: Mark the M0 design complete only after CI passes**

Change the header in `docs/superpowers/specs/2026-08-17-owlengine-m0-design.md` to:

```markdown
- Status: Approved
- Date: 2026-08-17
- Parent roadmap: [OwlEngine Roadmap Design](2026-08-17-owlengine-roadmap-design.md)
- Implementation status: Complete
```

- [ ] **Step 9: Run final repository verification**

```powershell
./scripts/configure.ps1 -Preset windows-vs2026 -CMakeArguments '-DOWL_WARNINGS_AS_ERRORS=ON'
cmake --build --preset windows-vs2026-debug
ctest --preset windows-vs2026-debug
cmake --build --preset windows-vs2026-relwithdebinfo
ctest --preset windows-vs2026-relwithdebinfo
rg -n "Vulkan|D3D12|RHI|RenderGraph|Renderer" engine samples tests
git diff --check
git status --short
```

Expected:

- Configure succeeds.
- Both builds succeed.
- Both CTest presets report zero failures.
- The scoped `rg` command returns no matches, confirming that M1+ graphics systems have not entered M0 code.
- `git diff --check` succeeds.
- `git status --short` shows only the intended README and M0 status edits.

- [ ] **Step 10: Commit the accepted M0 baseline**

```powershell
git add README.md docs/superpowers/specs/2026-08-17-owlengine-m0-design.md
git commit -m "docs: mark M0 engineering baseline complete"
```

Do not begin M1 Vulkan work in this commit. M1 starts with its own design and implementation plan.

- [ ] **Step 11: Push the final M0 status and verify CI at the final HEAD**

With explicit push authorization still in effect:

```powershell
git push origin master
git status --short --branch
```

Expected: the branch is synchronized with `origin/master` and the working tree is clean. Inspect the newly triggered `Windows Build` workflow and require it to pass for the final commit before reporting M0 complete.
