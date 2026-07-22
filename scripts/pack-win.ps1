# pack-win.ps1 -- build the Windows release zip (icmg-<ver>-win-x64.zip) + sha256 sidecar.
#
# Bundles icmg.exe + all runtime DLLs from the MSVC Release build into a zip,
# names it from the project version (auto-read from CMakeLists.txt), and writes
# a matching <zip>.sha256 sidecar. Mirrors the layout CI produces for
# Linux/macOS so all three platforms ship the same asset shape.
#
# Usage (from anywhere):
#   pwsh -File scripts/pack-win.ps1                 # auto: version from CMakeLists, build-msvc-full\Release
#   pwsh -File scripts/pack-win.ps1 -Version 2.20.0 # override version
#   pwsh -File scripts/pack-win.ps1 -BuildDir <dir> # override Release dir (holding *.exe/*.dll)
#   pwsh -File scripts/pack-win.ps1 -OutDir <dir>   # where the zip lands (default: repo root)
#
# ASCII-only (parses under PowerShell 5.1 and 7).

param(
    [string]$Version = '',
    [string]$BuildDir = '',
    [string]$OutDir = '',
    [string]$Arch = 'x64'
)
$ErrorActionPreference = 'Stop'

# --- repo root = parent of this script's directory ---
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = Split-Path -Parent $scriptDir

# --- version: param, else parse CMakeLists.txt project(icmg VERSION x.y.z ...) ---
if (-not $Version) {
    $cmake = Join-Path $repoRoot 'CMakeLists.txt'
    if (-not (Test-Path $cmake)) { throw "CMakeLists.txt not found at $cmake (pass -Version to override)" }
    $m = Select-String -Path $cmake -Pattern 'project\s*\(\s*icmg\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)' | Select-Object -First 1
    if (-not $m) { throw "Could not parse VERSION from $cmake (pass -Version to override)" }
    $Version = $m.Matches[0].Groups[1].Value
}

# --- build dir: param, else the usual MSVC Release output ---
if (-not $BuildDir) {
    $candidates = @(
        (Join-Path $repoRoot 'build-msvc-full\Release'),
        (Join-Path $repoRoot 'build-msvc-full'),
        (Join-Path $repoRoot 'build\Release'),
        (Join-Path $repoRoot 'build')
    )
    $BuildDir = $candidates | Where-Object { Test-Path (Join-Path $_ 'icmg.exe') } | Select-Object -First 1
    if (-not $BuildDir) { throw "icmg.exe not found in any known build dir. Build first, or pass -BuildDir." }
}
$exe = Join-Path $BuildDir 'icmg.exe'
if (-not (Test-Path $exe)) { throw "icmg.exe not found at $exe" }

if (-not $OutDir) { $OutDir = $repoRoot }

# --- sanity: does the built binary report the version we're packaging? ---
try {
    $reported = (& $exe --version 2>$null | Select-Object -First 1)
    if ($reported -and ($reported -notmatch [regex]::Escape($Version))) {
        Write-Warning "icmg.exe reports '$reported' but packaging version $Version -- rebuild if that's stale."
    }
} catch { Write-Warning "could not run icmg.exe --version: $_" }

$name = "icmg-$Version-win-$Arch"
$pkg  = Join-Path $env:TEMP "icmg-pkg-$Version"
if (Test-Path $pkg) { Remove-Item $pkg -Recurse -Force }
New-Item -ItemType Directory -Path $pkg | Out-Null

# copy the binary + every runtime DLL sitting next to it
Get-ChildItem "$BuildDir\*" -Include *.exe, *.dll | Copy-Item -Destination $pkg
$fileCount = (Get-ChildItem $pkg).Count

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$pkg\*" -DestinationPath $zip

$h   = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$sha = "$zip.sha256"
[IO.File]::WriteAllText($sha, "$h  $name.zip", [System.Text.Encoding]::ASCII)

$z = Get-Item $zip
Write-Host ("version:  {0}" -f $Version)
Write-Host ("build:    {0}" -f $BuildDir)
Write-Host ("zip:      {0}  ({1:N1} MB, {2} files)" -f $z.Name, ($z.Length / 1MB), $fileCount)
Write-Host ("sha256:   {0}" -f $h)
Write-Host ("sidecar:  {0}" -f (Split-Path -Leaf $sha))
Write-Host ""
Write-Host "Upload:   gh release upload v$Version `"$zip`" `"$sha`" --clobber"
