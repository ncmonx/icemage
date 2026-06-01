#requires -Version 5
# release-win.ps1 - build + package + upload the Windows binary to its GH release.
#
# Why this exists: CI auto-builds Linux + macOS on tag push, but Windows is built
# locally (MSVC + sccache + Vulkan). This script makes the local half a ONE-LINER
# and guarantees the uploaded EXE's --version matches the tag (catches the
# "shipped stale binary" class of bug before upload).
#
# Version is read from src/core/version.hpp (the single C++ source of truth) -
# NOT from a param - so the zip name + tag can never drift from the compiled EXE.
#
# Usage:
#   pwsh -File scripts/release-win.ps1                 # build + package + upload to v<ver>
#   pwsh -File scripts/release-win.ps1 -NoBuild        # package + upload an already-built EXE
#   pwsh -File scripts/release-win.ps1 -NoUpload       # build + package only (no gh upload)
#   pwsh -File scripts/release-win.ps1 -Repo ncmonx/icemage
param(
    [switch]$NoBuild,
    [switch]$NoUpload,
    [string]$Repo = 'ncmonx/icemage'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# --- version: single source of truth = src/core/version.hpp -------------------
$verLine = Get-Content 'src\core\version.hpp' | Select-String 'ICMG_VERSION\s*=\s*"' | Select-Object -First 1
if (-not $verLine) { throw 'cannot find ICMG_VERSION in src/core/version.hpp' }
$Version = ([regex]::Match($verLine.Line, '"([^"]+)"')).Groups[1].Value
Write-Host "version.hpp -> $Version" -ForegroundColor Cyan

# sanity: CMakeLists + .rc must agree (else build name/EXE drift)
$cmakeVer = ([regex]::Match((Get-Content 'CMakeLists.txt' -Raw), 'project\(icmg VERSION (\d+\.\d+\.\d+)')).Groups[1].Value
$rcVer    = ([regex]::Match((Get-Content 'src\icmg.rc'    -Raw), 'VALUE "FileVersion",\s+"([^"]+)"')).Groups[1].Value
if ($cmakeVer -ne $Version -or $rcVer -ne $Version) {
    throw "version drift: version.hpp=$Version CMakeLists=$cmakeVer icmg.rc=$rcVer. Run: pwsh scripts/bump-version.ps1 $Version"
}
Write-Host "CMakeLists + icmg.rc agree ($Version)" -ForegroundColor Green

# --- build --------------------------------------------------------------------
if (-not $NoBuild) {
    Write-Host "building (build.ps1 -Reconfigure)..." -ForegroundColor Cyan
    & pwsh -NoProfile -File (Join-Path $root 'build.ps1') -Reconfigure
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed (exit $LASTEXITCODE)" }
}

# --- locate built EXE ---------------------------------------------------------
$bd = 'C:\icmg-build\build-msvc-full'      # build.ps1 binaryDir (off-D: clutter)
$exe = @("$root\build-msvc-full\Release\icmg.exe", "$bd\Release\icmg.exe", "$bd\icmg.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { throw "icmg.exe not found - build first (omit -NoBuild)" }

# --- guard: EXE --version MUST match $Version (the whole point) ----------------
$pkg = "C:\Temp\icmg-pkg-$Version"
if (Test-Path $pkg) { Remove-Item $pkg -Recurse -Force }
New-Item -ItemType Directory $pkg | Out-Null
Copy-Item $exe "$pkg\icmg.exe" -Force

# DLL bundle (MSVC + ONNX + llama/ggml + Vulkan). Layout per build.ps1:
foreach ($d in 'libwinpthread-1.dll','onnxruntime.dll','onnxruntime_providers_shared.dll') {
    if (Test-Path "$bd\$d") { Copy-Item "$bd\$d" $pkg }
}
Get-ChildItem "$bd\bin\*.dll" -EA SilentlyContinue | Copy-Item -Destination $pkg   # ggml*, llama
if (Test-Path 'C:\Windows\System32\vulkan-1.dll') { Copy-Item 'C:\Windows\System32\vulkan-1.dll' $pkg }

# verify version from the PACKAGED exe (DLLs colocated)
Push-Location $pkg
$reported = (& '.\icmg.exe' --version 2>&1 | Select-Object -First 1)
Pop-Location
Write-Host "packaged exe reports: $reported" -ForegroundColor Cyan
if ($reported -notmatch [regex]::Escape($Version)) {
    throw "STALE BINARY: exe reports '$reported' but expected $Version. Rebuild with -Reconfigure (version.hpp TU must recompile)."
}
Write-Host "version match OK" -ForegroundColor Green

# --- zip + sha256 -------------------------------------------------------------
$zip = "C:\Temp\icmg-$Version-win-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$pkg\*" -DestinationPath $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$sha = "$zip.sha256"
[IO.File]::WriteAllText($sha, "$hash  icmg-$Version-win-x64.zip", [Text.ASCIIEncoding]::new())
Write-Host "zip: $zip" -ForegroundColor Green
Write-Host "sha256: $hash" -ForegroundColor Green
Get-ChildItem $pkg | Select-Object Name | Format-Table -HideTableHeaders

# --- upload -------------------------------------------------------------------
if ($NoUpload) {
    Write-Host "skip upload (-NoUpload). Artifacts staged at $zip" -ForegroundColor Yellow
    return
}
$tag = "v$Version"
Write-Host "uploading to $Repo $tag (--clobber)..." -ForegroundColor Cyan
& gh release upload $tag $zip $sha --repo $Repo --clobber
if ($LASTEXITCODE -ne 0) { throw "gh release upload failed (exit $LASTEXITCODE) - does release $tag exist?" }
Write-Host "uploaded $tag win-x64 zip + sha256." -ForegroundColor Green
& gh release view $tag --repo $Repo --json assets --jq '.assets[].name'
