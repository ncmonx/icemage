#requires -Version 5
# build.ps1 ” MSVC 2026 (VS 18) ship build for icemage
# C++23, Ninja, vcpkg manifest, full features + Vulkan
#
# Usage:
#   powershell -File build.ps1                              # build icmg (default)
#   powershell -File build.ps1 -Target both                 # icmg + icmg_test
#   powershell -File build.ps1 -Target test                 # icmg_test only
#   powershell -File build.ps1 -Target both -RunTests       # build + run full ctest
#   powershell -File build.ps1 -Target both -RunTests -TestFilter "updating_lock"  # filter
#   powershell -File build.ps1 -RunTests -TestFilter ".*"   # run tests (skip build if already built)
#   powershell -File build.ps1 -Reconfigure                 # re-configure (never touches third_party)
#   powershell -File build.ps1 -ShowLog                     # dump last log, no build
#   powershell -File build.ps1 -ShowLog -Lines 50           # last N lines
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

# ”” constants ””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””
$VS       = 'C:\Program Files\Microsoft Visual Studio\18\Enterprise'   # VS 2026
$vcvars   = "$VS\VC\Auxiliary\Build\vcvars64.bat"
$cmakeBin = "$VS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$ninjaBin = "$VS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$LogDir   = "$env:USERPROFILE\.icmg\build-logs"
$LogFile  = "$LogDir\msvc-build-latest.log"
$Jobs     = [Environment]::ProcessorCount          # auto parallel jobs

# Build dir on C:\ (not D:\) ” faster IO, no D:\ artifact clutter.
# Source stays on D:\; only compiled .obj/.exe go to C:\icmg-build\.
$BuildDir = 'C:\icmg-build\build-msvc-full'

Set-Location $PSScriptRoot

# ”” auto-read version from CMakeLists.txt (single source of truth) ””””””””””””
function Get-IcmgVersion {
    $line = Get-Content 'CMakeLists.txt' | Select-String 'project\(icmg VERSION' | Select-Object -First 1
    if ($line -match 'VERSION\s+([\d.]+)') { return $Matches[1] }
    return 'unknown'
}
$Version = Get-IcmgVersion

# ”” show-log shortcut ”””””””””””””””””””””””””””””””””””””””””””””””””””””””””
if ($ShowLog) {
    if (-not (Test-Path $LogFile)) { Write-Host 'No log yet.'; exit 0 }
    if ($Lines -gt 0) { Get-Content $LogFile -Tail $Lines } else { Get-Content $LogFile }
    exit 0
}

if (-not (Test-Path $vcvars)) { throw "VS 2026 not found: $vcvars" }

# ”” log setup ”””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””
New-Item -ItemType Directory -Force $LogDir | Out-Null
$LogTs  = "$LogDir\msvc-build-$(Get-Date -Format 'yyyyMMdd-HHmmss').log"
$sw1    = [IO.StreamWriter]::new($LogFile, $false, [Text.Encoding]::UTF8)
$sw2    = [IO.StreamWriter]::new($LogTs,   $false, [Text.Encoding]::UTF8)
function logline([string]$s) { Write-Host $s; $sw1.WriteLine($s); $sw1.Flush(); $sw2.WriteLine($s); $sw2.Flush() }
function close-logs { $sw1.Close(); $sw2.Close() }

# ”” vcvars: inject VS 2026 env vars into this PS session ””””””””””””””””””””””
function Set-VcVars {
    $tmp = [IO.Path]::GetTempFileName()
    cmd /c "`"$vcvars`" >nul && set" 2>$null | Set-Content $tmp -Encoding UTF8
    Get-Content $tmp | Where-Object { $_ -match '^([^=]+)=(.*)$' } | ForEach-Object {
        $k,$v = $Matches[1], $Matches[2]
        [Environment]::SetEnvironmentVariable($k, $v, 'Process')
    }
    Remove-Item $tmp -ErrorAction SilentlyContinue
}

# ”” run command, stream output line-by-line to tee ””””””””””””””””””””””””””””
function Invoke-Build([string]$Cmd) {
    $psi = [Diagnostics.ProcessStartInfo]::new('cmd.exe', "/c $Cmd 2>&1")
    $psi.UseShellExecute        = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $false
    $psi.CreateNoWindow         = $true
    $p = [Diagnostics.Process]::Start($psi)
    while (-not $p.StandardOutput.EndOfStream) { logline $p.StandardOutput.ReadLine() }
    $p.WaitForExit()
    return $p.ExitCode
}

# ”” Vulkan .obj guard ”””””””””””””””””””””””””””””””””””””””””””””””””””””””””
# mul_mm.comp.cpp = 147 MB †’ MSVC C1060 (out of heap).
# If stamp missing, mark .obj newer than .cpp so ninja skips recompile.
function Protect-VulkanObjs {
    $base  = "$BuildDir\third_party\llama.cpp\ggml\src\ggml-vulkan"
    $stamp = "$base\vulkan-shaders-gen-prefix\src\vulkan-shaders-gen-stamp\vulkan-shaders-gen-build"
    if (Test-Path $stamp) { return }
    logline '[vulkan-guard] stamp missing ” skipping 147MB shader recompile'
    New-Item -ItemType File -Force $stamp | Out-Null
    $now = Get-Date; $old = $now.AddHours(-1)
    Get-ChildItem $base -Filter '*.comp.cpp'   -ErrorAction SilentlyContinue | % { $_.LastWriteTime = $old }
    Get-ChildItem "$base\CMakeFiles\ggml-vulkan.dir" -Filter '*.comp.cpp.obj' -ErrorAction SilentlyContinue | % { $_.LastWriteTime = $now }
    logline '[vulkan-guard] done'
}

# ”” main ”””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””””
$ts = Get-Date -Format 'yyyyMMdd-HHmmss'
logline "=== icemage v$Version MSVC2026 [$ts] target=$Target jobs=$Jobs ==="
logline "log: $LogFile"
logline "archive: $LogTs"

$env:PATH = "$cmakeBin;$ninjaBin;$env:PATH"
$env:VCPKG_ROOT = 'C:\vcpkg'

logline '--- inject VS 2026 environment ---'
Set-VcVars

$clCmd = Get-Command cl.exe -ErrorAction SilentlyContinue
$cl = if ($clCmd) { $clCmd.Source } else { '(not found)' }
logline "cl: $cl"

# configure if needed (uses $BuildDir on C:\)
logline "build dir: $BuildDir"
New-Item -ItemType Directory -Force $BuildDir | Out-Null
# v1.79: for the test target, skip RC/manifest-embed at CONFIGURE time. The
# mt.exe manifest-embed reopens the 33MB icmg_test.exe post-link, racing
# Defender's real-time scan -> LNK1104. icmg_test needs no manifest. The ship
# binary (-Target icmg) keeps its manifest (B:/ SxS popup fix).
if ($Target -eq 'test') { $env:ICMG_SKIP_RC = '1' }
if ($Reconfigure -or -not (Test-Path "$BuildDir\CMakeCache.txt")) {
    logline '--- cmake configure (preset msvc-release) ---'
    # Override binaryDir to C:\icmg-build\build-msvc-full
    $rc = Invoke-Build "cmake -B `"$BuildDir`" --preset msvc-release"
    if ($rc -ne 0) { logline "!!! configure failed rc=$rc"; close-logs; exit $rc }
}

Protect-VulkanObjs

# build targets
$RC1 = 0; $RC2 = 0
if ($Target -in 'icmg','both') {
    logline '--- build icmg ---'
    $RC1 = Invoke-Build "cmake --build `"$BuildDir`" --target icmg --config Release --parallel $Jobs"
    logline "--- icmg rc=$RC1 ---"
}
if ($Target -in 'test','both') {
    logline '--- build icmg_test ---'
    $env:ICMG_SKIP_RC = '1'
    $RC2 = Invoke-Build "cmake --build `"$BuildDir`" --target icmg_test --config Release --parallel $Jobs"
    logline "--- icmg_test rc=$RC2 ---"
}

# ctest (optional)
$RCT = 0
if ($RunTests) {
    logline '--- ctest ---'
    $ctestArgs = "ctest --test-dir `"$BuildDir`" --output-on-failure --parallel $Jobs"
    if ($TestFilter) { $ctestArgs += " -R `"$TestFilter`"" }
    $RCT = Invoke-Build $ctestArgs
    logline "--- ctest rc=$RCT ---"
}

# copy ship binary to source tree + clean build dir
# Ninja generator: exe at $BuildDir\icmg.exe (no Release\ subdir)
# ONLY runs when the icmg target was actually built this invocation
# (NOT for -Target test, else the clean nukes icmg_test.exe).
$exeSrc  = if (Test-Path "$BuildDir\Release\icmg.exe") { "$BuildDir\Release\icmg.exe" } else { "$BuildDir\icmg.exe" }
$exeDest = "$PSScriptRoot\build-msvc-full\Release\icmg.exe"
if ($Target -in 'icmg','both' -and $RC1 -eq 0 -and (Test-Path $exeSrc)) {
    logline '--- copy ship binary to source tree ---'
    New-Item -ItemType Directory -Force "$PSScriptRoot\build-msvc-full\Release" | Out-Null
    Copy-Item $exeSrc $exeDest -Force
    logline "copied: $exeDest"
    # NOTE: do NOT clean $BuildDir here. It lives on C:\ (no D: clutter) and
    # wiping it forces a full recompile next build + races icmg_test link
    # (LNK1104). Incremental artifacts stay; third_party Vulkan .obj preserved.
}

# summary
logline ''
logline '===== SUMMARY ====='
$errs = Select-String $LogFile -Pattern ': error |: fatal error|LNK' -ErrorAction SilentlyContinue
if ($errs) { $errs | ForEach-Object { logline "  $($_.Line.Trim())" }; logline '  ^^^ ERRORS' }
else        { logline '  no errors' }
logline "rc: icmg=$RC1  icmg_test=$RC2  ctest=$RCT"
$verExe = if (Test-Path $exeDest) { $exeDest } elseif (Test-Path $exeSrc) { $exeSrc } else { $null }
if ($verExe) {
    $v = & $verExe --version 2>&1
    logline "version: $v"
    logline "zip name: icmg-$Version-win-x64.zip"
    logline "exe: $verExe"
} else { logline 'WARNING: icmg.exe not found' }
logline '==================='

close-logs
exit ([Math]::Max([Math]::Max($RC1, $RC2), $RCT))

