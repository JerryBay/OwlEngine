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
$requiresBootstrap = $isNewCheckout
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

$currentRevision = [string](& git -C $vcpkgRoot rev-parse HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or $currentRevision.Trim() -ne $revision) {
    $requiresBootstrap = $true

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
$vcpkgExecutable = Join-Path $vcpkgRoot 'vcpkg.exe'
$requiresBootstrap = $requiresBootstrap -or -not (Test-Path -LiteralPath $vcpkgExecutable)
if ($requiresBootstrap) {
    & $bootstrapScript -disableMetrics | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to bootstrap vcpkg. Exit code: $LASTEXITCODE"
    }
}

if (-not (Test-Path -LiteralPath $vcpkgExecutable)) {
    throw "The vcpkg executable was not created at $vcpkgExecutable"
}

Write-Output $vcpkgRoot
