@echo off
setlocal
REM v1.42.0: NO wipe path. CMake handles delta config automatically.
REM If wipe REALLY needed (generator/compiler/build-type change), do manually:
REM   rmdir /s /q build-msvc-full
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "D:\Data Kerja\Personal\AI\icm-graph"
cmake -B build-msvc-full -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=cl ^
    -DCMAKE_CXX_COMPILER=cl ^
    -DICMG_USE_MODULES=OFF ^
    -DICMG_NO_PCH=ON ^
    -DICMG_USE_ONNX=ON ^
    -DICMG_USE_TREESITTER=ON ^
    -DICMG_USE_LLAMA=ON ^
    2>&1
