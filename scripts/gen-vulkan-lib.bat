@echo off
REM v1.43+: Generate vulkan-1.lib MSVC import from system C:\Windows\System32\vulkan-1.dll.
REM LunarG SDK skipped — system DLL has all loader exports we need.
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul

set OUT_DIR=D:\Data Kerja\Personal\AI\icm-graph\third_party\vulkan-lib
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
cd /d "%OUT_DIR%"

REM Copy DLL for dumpbin (avoid System32 redirect issues).
copy /Y "C:\Windows\System32\vulkan-1.dll" .

echo ==^> Extracting exports from vulkan-1.dll ...
dumpbin /exports vulkan-1.dll > vulkan-1.exports.txt
if errorlevel 1 exit /b 1

echo ==^> Generating vulkan-1.def ...
echo LIBRARY vulkan-1 > vulkan-1.def
echo EXPORTS >> vulkan-1.def
powershell -NoProfile -Command "Get-Content vulkan-1.exports.txt | Where-Object { $_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)' } | ForEach-Object { $matches[1] } | Sort-Object -Unique" >> vulkan-1.def

echo ==^> Building vulkan-1.lib ...
lib /def:vulkan-1.def /out:vulkan-1.lib /machine:x64
if errorlevel 1 exit /b 1

echo ==^> Done.
dir vulkan-1.lib
