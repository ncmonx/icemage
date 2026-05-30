@echo off
REM ============================================================================
REM Canonical Windows build for icmg. ALWAYS use this for ship builds.
REM
REM HARD RULES (each one caused a shipped-wrong-binary incident before):
REM   1. --config Release  -> VS multi-config generator defaults to Debug
REM      WITHOUT this flag, silently shipping a Debug binary.
REM   2. Build BOTH icmg and icmg_test so ctest reflects the same sources.
REM   3. Release output lands at  build-msvc-full\Release\icmg.exe
REM      (NOT build-msvc-full\icmg.exe -- that is a stale leftover).
REM   4. Full log is ALWAYS written to %USERPROFILE%\.icmg\build-logs\ so
REM      errors can be inspected WITHOUT rebuilding just to see them.
REM   5. A compile error in icmg_lib does NOT fail the exe link -- it relinks
REM      the STALE lib and reports an OLD version. So this script greps the
REM      log for ": error"/"fatal error" and prints a summary + the built
REM      version every time. Treat a non-empty error summary as a failed build
REM      even if an .exe exists.
REM
REM Usage:  msvc-build-full.bat [icmg|test|both]   (default: both)
REM ============================================================================
setlocal enabledelayedexpansion
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0.."

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=both"

set "LOGDIR=%USERPROFILE%\.icmg\build-logs"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set "LOG=%LOGDIR%\msvc-build-latest.log"

REM Truncate the log up front so single-target runs never show stale errors
REM from a previous build in the error summary below.
echo === build start (target=%TARGET%) ===> "%LOG%"

set RC1=0
set RC2=0

if /i "%TARGET%"=="test" goto buildtest

echo === BUILD icmg (Release, with RC manifest) ===>> "%LOG%"
cmake --build build-msvc-full --target icmg --config Release --parallel >> "%LOG%" 2>&1
set RC1=%ERRORLEVEL%
if /i "%TARGET%"=="icmg" goto summary

:buildtest
echo === BUILD icmg_test (Release, ICMG_SKIP_RC) ===>> "%LOG%"
set ICMG_SKIP_RC=1
cmake --build build-msvc-full --target icmg_test --config Release --parallel >> "%LOG%" 2>&1
set RC2=%ERRORLEVEL%

:summary
echo.
echo ============ BUILD ERROR SUMMARY ============
findstr /i /c:": error " /c:": fatal error" /c:"LNK" "%LOG%"
if errorlevel 1 (echo   ^(no compiler/linker errors found in log^)) else (echo   ^^^ ERRORS ABOVE -- build is NOT clean)
echo =============================================
echo cmake rc: icmg=%RC1% icmg_test=%RC2%
echo Full log: %LOG%
if exist "build-msvc-full\Release\icmg.exe" (
  echo Built exe version:
  "build-msvc-full\Release\icmg.exe" --version
) else (
  echo WARNING: build-msvc-full\Release\icmg.exe not found
)
endlocal
