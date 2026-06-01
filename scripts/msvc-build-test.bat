@echo off
setlocal
set ICMG_SKIP_RC=1
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "D:\Data Kerja\Personal\AI\icm-graph"
cmake --build build-msvc-full --target icmg_test --config Release --parallel 2>&1
