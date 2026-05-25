@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "D:\Data Kerja\Personal\AI\icm-graph"
cmake --build build-msvc --target icmg --parallel
