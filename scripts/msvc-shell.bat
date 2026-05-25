@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cl 2>&1 | findstr /C:"Microsoft" /C:"Version"
echo --- cmake ---
cmake --version | findstr "version"
echo --- ninja ---
where ninja 2>nul && ninja --version
