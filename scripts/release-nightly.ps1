#!/usr/bin/env pwsh
#Requires -Version 5.1
# Publishes the rolling `dev` GitHub pre-release for EdithFinchHeadTracking.
# Invoked by `pixi run release nightly`. All build/package/hash/publish logic
# lives in cameraunlock-core/powershell/NightlyRelease.psm1.

[CmdletBinding()]
param([switch]$AllowDirty)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$cmakeLists = Get-Content (Join-Path $ProjectRoot 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(EdithFinchHeadTracking VERSION (\d+\.\d+\.\d+)') {
    throw 'Could not parse version from CMakeLists.txt'
}
$version = $Matches[1]

Publish-NightlyBuild `
    -ModId 'edith-finch' `
    -ModName 'EdithFinchHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -BuildCommand 'pixi run build' `
    -AllowDirty:$AllowDirty
