#!/usr/bin/env pwsh
#Requires -Version 5.1
# Deploy the built EdithFinchHeadTracking.asi into the game's exe directory
# for local testing.
#
# Usage: deploy.ps1 [GAME_PATH] [-Configuration Debug|Release]
# Game detection order matches install.cmd: explicit path -> EDITH_FINCH_PATH
# env var -> Steam registry / library folders -> games.json.

param(
    [Parameter(Position = 0)]
    [string]$GamePath,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$asi = Join-Path $projectDir "build/$Configuration/EdithFinchHeadTracking.asi"
if (-not (Test-Path $asi)) {
    Write-Error "Build output not found: $asi. Run 'pixi run build' first."
    exit 1
}

if (-not $GamePath) {
    Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $GamePath = Find-GamePath -GameId 'edith-finch'
}

if (-not $GamePath -or -not (Test-Path $GamePath)) {
    Write-Error "Could not locate What Remains of Edith Finch. Set EDITH_FINCH_PATH or pass the install path as the first argument."
    exit 1
}

$exeDir = Join-Path $GamePath 'FinchGame\Binaries\Win64'
if (-not (Test-Path $exeDir)) {
    Write-Error "Expected exe directory not found: $exeDir"
    exit 1
}

Copy-Item $asi -Destination $exeDir -Force
Write-Host "Deployed: $asi -> $exeDir" -ForegroundColor Green

# UE4SS already proxies dwmapi.dll in this game, so the Ultimate ASI Loader
# proxies winmm.dll instead - the shipping exe imports it statically and it is
# free. Matches ASI_LOADER_NAME in install.cmd.
$loaderTarget = Join-Path $exeDir 'winmm.dll'
if (-not (Test-Path $loaderTarget)) {
    $vendorLoader = Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll'
    if (-not (Test-Path $vendorLoader)) {
        Write-Error "Vendored ASI loader missing: $vendorLoader. Run 'pixi run update-deps' and commit the result."
        exit 1
    }
    Copy-Item $vendorLoader -Destination $loaderTarget -Force
    Write-Host "Installed Ultimate ASI Loader as winmm.dll" -ForegroundColor Green
}
