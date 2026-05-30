#requires -Version 5
# build.ps1 — MSVC 2026 (VS 18) ship build for icemage
# C++23, Ninja, vcpkg manifest, full features + Vulkan
#
# Usage:
#   pwsh -File build.ps1                              # build icmg (default)
#   pwsh -File build.ps1 -Target both                 # icmg + icmg_test
#   pwsh -File build.ps1 -Target test                 # icmg_test only
#   pwsh -File build.ps1 -Target both -RunTests       # build + run full ctest
#   pwsh -File build.ps1 -Target both -RunTests -TestFilter "updating_lock"  # filter
#   pwsh -File build.ps1 -RunTests -TestFilter ".*"   # run tests (skip build if already built)
#   pwsh -File build.ps1 -Reconfigure                 # re-configure (never touches third_party)
#   pwsh -File build.ps1 -ShowLog                     # dump last log, no build
#   pwsh -File build.ps1 -ShowLog -Lines 50           # last N lines
#
# Logs (real-time tee, readable any time without rebuild):
#   %USERPROFILE%\.icmg\build-logs\msvc-build-latest.log
#   %USERPROFILE%\.icmg\build-logs\msvc-build-<ts>.log

param(
    [ValidateSet('icmg','test','both')] [string]$Target = 'icmg',
    [switch]$Reconfigure,
    [switch]$RunTests,      # run ctest after build (requires -Target test or both)
    [string]$TestFilter,   # ctest -R regex filter, e.g. "test_updating_lock"
    [switch]$ShowLog,
    [int]$Lines = 0
)
$ErrorActionPreference = 'Stop'

# ── constants ──────────────────────────────────────────────────────────────────
$VS       = 'C:\Program Files\Microsoft Visual Studio\18\Enterprise'   # VS 2026
$vcvars   = "$VS\VC\Auxiliary\Build\vcvars64.bat"
$cmakeBin = "$VS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$ninjaBin = "$VS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$LogDir   = "$env:USERPROFILE\.icmg\build-logs"
$LogFile  = "$LogDir\msvc-build-latest.log"
$Jobs     = [Environment]::ProcessorCount          # auto parallel jobs

Set-Location $PSScriptRoot

# ── auto-read version from CMakeLists.txt (single source of truth) ────────────
function Get-IcmgVersion {
    $line = Get-Content 'CMakeLists.txt' | Select-String 'project\(icmg VERSION' | Select-Object -First 1
    if ($line -match 'VERSION\s+([\d.]+)') { return $Matches[1] }
    return 'unknown'
}
$Version = Get-IcmgVersion

# ── show-log shortcut ─────────────────────────────────────────────────────────
if ($ShowLog) {
    if (-not (Test-Path $LogFile)) { Write-Host 'No log yet.'; exit 0 }
    if ($Lines -gt 0) { Get-Content $LogFile -Tail $Lines } else { Get-Content $LogFile }
    exit 0
}

if (-not (Test-Path $vcvars)) { throw "VS 2026 not found: $vcvars" }

# ── log setup ─────────────────────────────────────────────────────────────────
New-Item -ItemType Directory -Force $LogDir | Out-Null
$LogTs  = "$LogDir\msvc-build-$(Get-Date -Format 'yyyyMMdd-HHmmss').log"
$sw1    = [IO.StreamWriter]::new($LogFile, $false, [Text.Encoding]::UTF8)
$sw2    = [IO.StreamWriter]::new($LogTs,   $false, [Text.Encoding]::UTF8)
function tee([string]$s) { Write-Host $s; $sw1.WriteLine($s); $sw1.Flush(); $sw2.WriteLine($s); $sw2.Flush() }
function close-logs { $sw1.Close(); $sw2.Close() }

# ── vcvars: inject VS 2026 env vars into this PS session ──────────────────────
function Set-VcVars {
    $tmp = [IO.Path]::GetTempFileName()
    cmd /c "`"$vcvars`" >nul && set" 2>$null | Set-Content $tmp -Encoding UTF8
    Get-Content $tmp | Where-Object { $_ -match '^([^=]+)=(.*)$' } | ForEach-Object {
        $k,$v = $Matches[1], $Matches[2]
        [Environment]::SetEnvironmentVariable($k, $v, 'Process')
    }
    Remove-Item $tmp -ErrorAction SilentlyContinue
}

# ── run command, stream output line-by-line to tee ────────────────────────────
function Invoke-Build([string]$Cmd) {
    $psi = [Diagnostics.ProcessStartInfo]::new('cmd.exe', "/c $Cmd 2>&1")
    $psi.UseShellExecute        = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $false
    $psi.CreateNoWindow         = $true
    $p = [Diagnostics.Process]::Start($psi)
    while (-not $p.StandardOutput.EndOfStream) { tee $p.StandardOutput.ReadLine() }
    $p.WaitForExit()
    return $p.ExitCode
}

# ── Vulkan .obj guard ─────────────────────────────────────────────────────────
# mul_mm.comp.cpp = 147 MB → MSVC C1060 (out of heap).
# If stamp missing, mark .obj newer than .cpp so ninja skips recompile.
function Protect-VulkanObjs {
    $base  = 'build-msvc-full\third_party\llama.cpp\ggml\src\ggml-vulkan'
    $stamp = "$base\vulkan-shaders-gen-prefix\src\vulkan-shaders-gen-stamp\vulkan-shaders-gen-build"
    if (Test-Path $stamp) { return }
    tee '[vulkan-guard] stamp missing — skipping 147MB shader recompile'
    New-Item -ItemType File -Force $stamp | Out-Null
    $now = Get-Date; $old = $now.AddHours(-1)
    Get-ChildItem $base -Filter '*.comp.cpp'   -ErrorAction SilentlyContinue | % { $_.LastWriteTime = $old }
    Get-ChildItem "$base\CMakeFiles\ggml-vulkan.dir" -Filter '*.comp.cpp.obj' -ErrorAction SilentlyContinue | % { $_.LastWriteTime = $now }
    tee '[vulkan-guard] done'
}

# ── main ───────────────────────────────────────────────────────────────────────
tee "=== icemage v$Version — MSVC 2026 build | $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') | target=$Target | jobs=$Jobs ==="
tee "log: $LogFile"
tee "archive: $LogTs"

$env:PATH = "$cmakeBin;$ninjaBin;$env:PATH"
$env:VCPKG_ROOT = 'C:\vcpkg'

tee '--- inject VS 2026 environment ---'
Set-VcVars

$cl = (Get-Command cl.exe -ErrorAction SilentlyContinue)?.Source
tee "cl: $cl"

# configure if needed
if ($Reconfigure -or -not (Test-Path 'build-msvc-full\CMakeCache.txt')) {
    tee '--- cmake configure (preset msvc-release) ---'
    $rc = Invoke-Build 'cmake --preset msvc-release'
    if ($rc -ne 0) { tee "!!! configure failed rc=$rc"; close-logs; exit $rc }
}

Protect-VulkanObjs

# build targets
$RC1 = 0; $RC2 = 0
if ($Target -in 'icmg','both') {
    tee '--- build icmg ---'
    $RC1 = Invoke-Build "cmake --build build-msvc-full --target icmg --config Release --parallel $Jobs"
    tee "--- icmg rc=$RC1 ---"
}
if ($Target -in 'test','both') {
    tee '--- build icmg_test ---'
    $env:ICMG_SKIP_RC = '1'
    $RC2 = Invoke-Build "cmake --build build-msvc-full --target icmg_test --config Release --parallel $Jobs"
    tee "--- icmg_test rc=$RC2 ---"
}

# ctest (optional)
$RCT = 0
if ($RunTests) {
    if ($Target -eq 'icmg') {
        tee '[ctest] WARNING: -Target icmg only — icmg_test not built, switching to run anyway'
    }
    tee '--- ctest ---'
    $ctestArgs = "ctest --test-dir build-msvc-full --output-on-failure --parallel $Jobs"
    if ($TestFilter) { $ctestArgs += " -R `"$TestFilter`"" }
    $RCT = Invoke-Build $ctestArgs
    tee "--- ctest rc=$RCT ---"
}

# summary
tee ''
tee '===== SUMMARY ====='
$errs = Select-String $LogFile -Pattern ': error |: fatal error|LNK' -ErrorAction SilentlyContinue
if ($errs) { $errs | % { tee "  $($_.Line.Trim())" }; tee '  ^^^ ERRORS' }
else        { tee '  no errors' }
tee "rc: icmg=$RC1  icmg_test=$RC2  ctest=$RCT"
if (Test-Path 'build-msvc-full\Release\icmg.exe') {
    $v = & 'build-msvc-full\Release\icmg.exe' --version 2>&1
    tee "version: $v"
    tee "zip name: icmg-$Version-win-x64.zip"
} else { tee 'WARNING: icmg.exe not found' }
tee '==================='

close-logs
exit ([Math]::Max([Math]::Max($RC1, $RC2), $RCT))
