#requires -Version 5
# bump-version.ps1 - single-command version bump across ALL source-of-truth sites.
#
# Icemage embeds its version in THREE places that MUST stay in lockstep, else the
# shipped binary reports a stale version (the `project(VERSION)` in CMakeLists is
# only the zip-name; the EXE version comes from src/core/version.hpp):
#   1. src/core/version.hpp   ICMG_VERSION   (C++ runtime --version, MCP, update-check)
#   2. src/icmg.rc            FILEVERSION / PRODUCTVERSION + string values (Win EXE resource)
#   3. CMakeLists.txt         project(icmg VERSION ...)  (zip name + CPack)
#
# Usage:
#   pwsh -File scripts/bump-version.ps1 1.89.0
#   pwsh -File scripts/bump-version.ps1 1.89.0 -DryRun     # show diff, write nothing
#
# After bump: rebuild with -Reconfigure so the version TU + .rc recompile.
#   pwsh -File build.ps1 -Reconfigure
param(
    [Parameter(Mandatory)] [string]$Version,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version must be MAJOR.MINOR.PATCH (e.g. 1.89.0); got '$Version'"
}
$parts = $Version.Split('.')
$comma = "$($parts[0]),$($parts[1]),$($parts[2]),0"   # 1,89,0,0  for .rc FILEVERSION

# Resolve repo root = parent of this script's dir.
$root = Split-Path -Parent $PSScriptRoot
function P($rel) { Join-Path $root $rel }

$edits = @()   # @{ File; Pattern; Replacement; Label }

$edits += @{ File = P 'src\core\version.hpp'
    Pattern = '(ICMG_VERSION\s*=\s*")[^"]+(")'
    Replacement = "`${1}$Version`${2}"; Label = 'version.hpp ICMG_VERSION' }

$edits += @{ File = P 'CMakeLists.txt'
    Pattern = '(project\(icmg VERSION )\d+\.\d+\.\d+'
    Replacement = "`${1}$Version"; Label = 'CMakeLists project(VERSION)' }

$edits += @{ File = P 'src\icmg.rc'
    Pattern = '(FILEVERSION\s+)\d+,\d+,\d+,\d+'
    Replacement = "`${1}$comma"; Label = 'icmg.rc FILEVERSION' }
$edits += @{ File = P 'src\icmg.rc'
    Pattern = '(PRODUCTVERSION\s+)\d+,\d+,\d+,\d+'
    Replacement = "`${1}$comma"; Label = 'icmg.rc PRODUCTVERSION' }
$edits += @{ File = P 'src\icmg.rc'
    Pattern = '(VALUE "FileVersion",\s+")[^"]+(")'
    Replacement = "`${1}$Version`${2}"; Label = 'icmg.rc FileVersion string' }
$edits += @{ File = P 'src\icmg.rc'
    Pattern = '(VALUE "ProductVersion",\s+")[^"]+(")'
    Replacement = "`${1}$Version`${2}"; Label = 'icmg.rc ProductVersion string' }

$changed = 0
foreach ($e in $edits) {
    if (-not (Test-Path $e.File)) { throw "missing file: $($e.File)" }
    $raw = Get-Content -Raw $e.File
    $new = [regex]::Replace($raw, $e.Pattern, $e.Replacement)
    if ($new -eq $raw) {
        Write-Host "  [skip] $($e.Label) - already $Version or pattern not found" -ForegroundColor DarkGray
        continue
    }
    if ($DryRun) {
        Write-Host "  [dry ] $($e.Label)" -ForegroundColor Yellow
    } else {
        # preserve original encoding (no BOM); .rc + .hpp are ASCII/UTF-8.
        [IO.File]::WriteAllText($e.File, $new, [Text.UTF8Encoding]::new($false))
        Write-Host "  [ok  ] $($e.Label)" -ForegroundColor Green
    }
    $changed++
}

Write-Host ""
if ($DryRun) {
    Write-Host "DryRun: $changed site(s) would change to $Version" -ForegroundColor Cyan
} else {
    Write-Host "Bumped $changed site(s) to $Version." -ForegroundColor Cyan
    Write-Host "NEXT: pwsh -File build.ps1 -Reconfigure   (recompiles version TU + .rc)" -ForegroundColor Cyan
}
