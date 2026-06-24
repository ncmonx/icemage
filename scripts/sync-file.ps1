#requires -Version 5
# sync-file.ps1 -- Copy a file from src to dst, then TOUCH dst's timestamp to
# now so ninja/MSBuild considers it newer than any cached .obj.
#
# Problem: Copy-Item preserves the SOURCE file's LastWriteTime. If the existing
# .obj in the build dir is NEWER than the copied source (common when the work-
# tree file was last committed days ago), the build tool skips recompilation
# -> stale .obj -> LNK2019 unresolved externals at link time.
#
# Usage:
#   pwsh -File scripts\sync-file.ps1 <src> <dst>
#   pwsh -File scripts\sync-file.ps1 <src> <dst-dir\>  # dst is a directory
#
# Example (release sync):
#   pwsh -File scripts\sync-file.ps1 src\imem\memory_store.cpp C:\Temp\icmg-public\src\imem\memory_store.cpp
#   pwsh -File scripts\sync-file.ps1 src\imem\memory_store.hpp C:\Temp\icmg-public\src\imem\

param(
    [Parameter(Mandatory)][string]$Src,
    [Parameter(Mandatory)][string]$Dst
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Src)) { throw "Source not found: $Src" }

# If dst is a directory, append the filename
if ((Test-Path $Dst -PathType Container) -or $Dst.EndsWith('\') -or $Dst.EndsWith('/')) {
    $Dst = Join-Path $Dst (Split-Path $Src -Leaf)
}

# Ensure destination directory exists
$dstDir = Split-Path $Dst -Parent
if ($dstDir -and -not (Test-Path $dstDir)) { New-Item -ItemType Directory $dstDir -Force | Out-Null }

Copy-Item $Src $Dst -Force
# CRITICAL: touch to now so ninja sees it as newer than any cached .obj
(Get-Item $Dst).LastWriteTime = Get-Date

$srcSize = (Get-Item $Src).Length
Write-Host "synced: $Src -> $Dst ($srcSize B, touched $(Get-Date -Format 'HH:mm:ss'))" -ForegroundColor Green
