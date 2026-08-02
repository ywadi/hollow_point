# Install the pinned build toolchain into .harness\.
#
#   .\bootstrap.ps1     then    .harness\zig\0.16.0\zig.exe build
#
# Installs Zig, CMake and Ninja -- everything the build needs. Nothing else has
# to be present on the host.
#
# CMake is pinned to the 3.x line on purpose. CMake 4 rejects
# `cmake_minimum_required(VERSION <3.5)`, and DiligentEngine's vendored
# third-party libraries still declare 2.8. 3.31 is also new enough for
# ozz-animation, which requires 3.30.
#
# Windows counterpart of bootstrap.sh. Keep the versions and hashes in sync with
# that script and with build.zig.

$ErrorActionPreference = 'Stop'

$ZigVersion   = '0.16.0'
$CMakeVersion = '3.31.12'
$NinjaVersion = '1.13.2'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Dl   = Join-Path $Root '.harness\dl'

if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
    Write-Error "unsupported host architecture '$env:PROCESSOR_ARCHITECTURE' (need AMD64)"
}

New-Item -ItemType Directory -Force -Path $Dl | Out-Null

function Get-Pinned {
    param([string]$Url, [string]$Name, [string]$Sha)

    $path = Join-Path $Dl $Name
    if (-not (Test-Path $path)) {
        Write-Host "==> downloading $Name"
        # Progress rendering makes Invoke-WebRequest an order of magnitude slower.
        $prev = $ProgressPreference
        $ProgressPreference = 'SilentlyContinue'
        try {
            Invoke-WebRequest -Uri $Url -OutFile "$path.part"
            Move-Item "$path.part" $path
        } finally {
            $ProgressPreference = $prev
        }
    }

    $actual = (Get-FileHash -Algorithm SHA256 -Path $path).Hash.ToLower()
    if ($actual -ne $Sha) {
        # A corrupt download must not be reused on the next run.
        Remove-Item $path -Force
        Write-Error "checksum mismatch for $Name`n  expected $Sha`n  actual   $actual"
    }
    return $path
}

# Extract <archive> into .harness\<tool>\, then rename the single top-level
# directory it unpacks to the pinned version number.
function Install-Archive {
    param([string]$Archive, [string]$Tool, [string]$StagedName, [string]$Dest)

    $parent = Join-Path $Root ".harness\$Tool"
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $staged = Join-Path $parent $StagedName
    if (Test-Path $Dest)   { Remove-Item $Dest   -Recurse -Force }
    if (Test-Path $staged) { Remove-Item $staged -Recurse -Force }
    Expand-Archive -Path $Archive -DestinationPath $parent -Force
    Move-Item $staged $Dest
}

# --- zig ---------------------------------------------------------------------

$ZigDir = Join-Path $Root ".harness\zig\$ZigVersion"
if (Test-Path (Join-Path $ZigDir 'zig.exe')) {
    Write-Host "zig $ZigVersion already present"
} else {
    $name = "zig-x86_64-windows-$ZigVersion.zip"
    $a = Get-Pinned "https://ziglang.org/download/$ZigVersion/$name" $name `
                    '68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e'
    Write-Host "==> installing zig $ZigVersion"
    Install-Archive $a 'zig' "zig-x86_64-windows-$ZigVersion" $ZigDir
}
& (Join-Path $ZigDir 'zig.exe') version | Out-Null

# --- cmake -------------------------------------------------------------------

$CMakeDir = Join-Path $Root ".harness\cmake\$CMakeVersion"
if (Test-Path (Join-Path $CMakeDir 'bin\cmake.exe')) {
    Write-Host "cmake $CMakeVersion already present"
} else {
    $name = "cmake-$CMakeVersion-windows-x86_64.zip"
    $a = Get-Pinned "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/$name" $name `
                    '0c4baa40f28b3f8225eb3fdf6946c987b4fe901403b4eaf2fbbd9378100aaa0c'
    Write-Host "==> installing cmake $CMakeVersion"
    Install-Archive $a 'cmake' "cmake-$CMakeVersion-windows-x86_64" $CMakeDir
}
& (Join-Path $CMakeDir 'bin\cmake.exe') --version | Out-Null

# --- ninja -------------------------------------------------------------------
#
# ninja-win.zip holds ninja.exe at the root, so unpack straight into place.

$NinjaDir = Join-Path $Root ".harness\ninja\$NinjaVersion"
if (Test-Path (Join-Path $NinjaDir 'ninja.exe')) {
    Write-Host "ninja $NinjaVersion already present"
} else {
    $a = Get-Pinned "https://github.com/ninja-build/ninja/releases/download/v$NinjaVersion/ninja-win.zip" `
                    'ninja-win.zip' '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'
    Write-Host "==> installing ninja $NinjaVersion"
    if (Test-Path $NinjaDir) { Remove-Item $NinjaDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $NinjaDir | Out-Null
    Expand-Archive -Path $a -DestinationPath $NinjaDir -Force
}
& (Join-Path $NinjaDir 'ninja.exe') --version | Out-Null

Write-Host ''
Write-Host 'toolchain ready in .harness\'
Write-Host "  zig    $ZigVersion"
Write-Host "  cmake  $CMakeVersion"
Write-Host "  ninja  $NinjaVersion"
Write-Host ''
Write-Host "next:  $ZigDir\zig.exe build all"
