#requires -Version 5.1
<#
.SYNOPSIS
Builds Aurora's Windows x64 VST3 and runs the eight headless test suites.
.DESCRIPTION
Requires Windows, Git, Python 3.10+, and Visual Studio 2022 (or Build Tools)
with Desktop development with C++ and a Windows SDK. No administrator access
is needed. CMake 3.31.6 is installed into .tools-windows; the exact JUCE commit
is downloaded into the build directory. Existing vendored JUCE is untouched.
The first run needs an internet connection. No plug-in is installed into a DAW.
.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts/build-windows.ps1
.EXAMPLE
pwsh -File scripts/build-windows.ps1 -GuiTests -Jobs 4
#>
[CmdletBinding()]
param(
    [string] $BuildDirectory = 'build-windows',
    [ValidateRange(1, 64)]
    [int] $Jobs = 4,
    [switch] $GuiTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Checked {
    param([string] $File, [string[]] $Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $File $($Arguments -join ' ')"
    }
}

if ($env:OS -ne 'Windows_NT') {
    throw 'This script requires Windows. On macOS, use scripts/build.sh.'
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'Aurora Windows builds require a 64-bit operating system.'
}

$projectDirectory = Split-Path -Parent $PSScriptRoot
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectDirectory $BuildDirectory))
}

$gitCommand = Get-Command git -CommandType Application -ErrorAction SilentlyContinue
if (-not $gitCommand) {
    throw 'Git for Windows is required. Install it and reopen your terminal.'
}
$git = $gitCommand.Source

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'Install Visual Studio 2022 or its Build Tools with Desktop development with C++ and a Windows SDK.'
}
$visualStudio = @(Invoke-Checked $vswhere @(
    '-latest', '-version', '[17.0,18.0)', '-products', '*',
    '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    '-property', 'installationPath'
))
if ($visualStudio.Count -eq 0) {
    throw 'Visual Studio 2022 C++ tools were not found. Add Desktop development with C++ and a Windows SDK in Visual Studio Installer.'
}

$pythonLauncher = Get-Command py -CommandType Application -ErrorAction SilentlyContinue
$pythonPrefix = @()
if ($pythonLauncher) {
    $python = $pythonLauncher.Source
    $pythonPrefix = @('-3')
} else {
    $pythonCommand = Get-Command python -CommandType Application -ErrorAction SilentlyContinue
    if (-not $pythonCommand) {
        throw 'Python 3.10 or newer is required. Install Python with its command-line launcher, then reopen your terminal.'
    }
    $python = $pythonCommand.Source
}
# No embedded quotes: Windows PowerShell 5.1 uses legacy native-argument quoting.
Invoke-Checked $python ($pythonPrefix + @('-c', 'import sys; assert sys.version_info >= (3, 10)'))

$toolsDirectory = Join-Path $projectDirectory '.tools-windows'
$localPython = Join-Path $toolsDirectory 'Scripts/python.exe'
if (-not (Test-Path -LiteralPath $localPython -PathType Leaf)) {
    Invoke-Checked $python ($pythonPrefix + @('-m', 'venv', $toolsDirectory))
}
$cmake = Join-Path $toolsDirectory 'Scripts/cmake.exe'
$ctest = Join-Path $toolsDirectory 'Scripts/ctest.exe'
$hasPinnedCmake = $false
if (Test-Path -LiteralPath $cmake -PathType Leaf) {
    $cmakeVersion = @(Invoke-Checked $cmake @('--version'))
    $hasPinnedCmake = $cmakeVersion.Count -gt 0 -and $cmakeVersion[0] -eq 'cmake version 3.31.6'
}
if (-not $hasPinnedCmake) {
    Invoke-Checked $localPython @('-m', 'pip', 'install', '--disable-pip-version-check', '--no-input', 'cmake==3.31.6')
}

$juceCommit = '51a8a6d7aeae7326956d747737ccf1575e61e209'
$jucePath = Join-Path $buildPath 'deps/JUCE'
if (-not (Test-Path -LiteralPath (Join-Path $jucePath '.git'))) {
    if ((Test-Path -LiteralPath $jucePath) -and @(Get-ChildItem -LiteralPath $jucePath -Force).Count -ne 0) {
        throw "Dependency path already contains files and is not a Git checkout: $jucePath. Choose another -BuildDirectory."
    }
    $downloadPath = "$jucePath.download-$([Guid]::NewGuid().ToString('N'))"
    try {
        New-Item -ItemType Directory -Force -Path $downloadPath | Out-Null
        Invoke-Checked $git @('init', $downloadPath)
        Invoke-Checked $git @('-C', $downloadPath, 'config', 'core.autocrlf', 'false')
        Invoke-Checked $git @('-C', $downloadPath, 'remote', 'add', 'origin', 'https://github.com/juce-framework/JUCE.git')
        Invoke-Checked $git @('-C', $downloadPath, 'fetch', '--depth', '1', 'origin', $juceCommit)
        Invoke-Checked $git @('-C', $downloadPath, 'checkout', '--detach', 'FETCH_HEAD')
        if (Test-Path -LiteralPath $jucePath) {
            # An existing path was checked to be empty above. Non-recursive
            # removal also fails safely if another process has since filled it.
            Remove-Item -LiteralPath $jucePath
        }
        Move-Item -LiteralPath $downloadPath -Destination $jucePath
    } finally {
        if (Test-Path -LiteralPath $downloadPath) {
            Remove-Item -LiteralPath $downloadPath -Recurse -Force
        }
    }
}
$actualCommit = (Invoke-Checked $git @('-C', $jucePath, 'rev-parse', 'HEAD')).Trim()
if ($actualCommit -ne $juceCommit) {
    throw "JUCE dependency is not the pinned commit $juceCommit. Choose a fresh -BuildDirectory; existing files have been preserved."
}
$juceChanges = @(Invoke-Checked $git @('-C', $jucePath, 'status', '--porcelain', '--untracked-files=all'))
if ($juceChanges.Count -ne 0) {
    throw 'The pinned JUCE checkout contains local changes. Choose a fresh -BuildDirectory; existing files have been preserved.'
}

$guiOption = if ($GuiTests) { 'ON' } else { 'OFF' }
Invoke-Checked $cmake @(
    '-S', $projectDirectory, '-B', $buildPath,
    '-G', 'Visual Studio 17 2022', '-A', 'x64', '-T', 'host=x64',
    "-DAURORA_JUCE_SOURCE_DIR=$jucePath", "-DAURORA_BUILD_GUI_TESTS=$guiOption"
)
Invoke-Checked $cmake @('--build', $buildPath, '--config', 'Release', '--parallel', "$Jobs")
Invoke-Checked $ctest @('--test-dir', $buildPath, '-C', 'Release', '--output-on-failure', '--no-tests=error')

$vst3 = Join-Path $buildPath 'Aurora_artefacts/Release/VST3/Aurora.vst3'
$module = Join-Path $vst3 'Contents/x86_64-win/Aurora.vst3'
if (-not (Test-Path -LiteralPath $module -PathType Leaf)) {
    throw "The expected Windows x64 VST3 module is missing: $module"
}
Write-Host "Windows x64 build and headless tests succeeded. VST3 bundle: $vst3"
Write-Host 'Native DAW playback, high-DPI, and GPU visual checks still require a Windows desktop.'
