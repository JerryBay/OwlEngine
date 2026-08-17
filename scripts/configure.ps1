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

$cmakeVersionOutput = (& cmake --version)
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to determine the installed CMake version.'
}

$cmakeVersionLine = $cmakeVersionOutput | Select-Object -First 1
if ($cmakeVersionLine -notmatch '^cmake version (?<version>\d+\.\d+\.\d+)') {
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
