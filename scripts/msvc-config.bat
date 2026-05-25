@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "D:\Data Kerja\Personal\AI\icm-graph"
REM v1.41.x build speed: do NOT wipe build-msvc by default — cmake handles
REM delta config. Pass FRESH=1 to force wipe when toolchain/flags change:
REM   set FRESH=1 && scripts\msvc-config.bat
if defined FRESH if exist build-msvc rmdir /s /q build-msvc
cmake -B build-msvc -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=cl ^
    -DCMAKE_CXX_COMPILER=cl ^
    -DICMG_USE_MODULES=ON ^
    -DCMAKE_EXPERIMENTAL_CXX_IMPORT_STD=451f2fe2-a8a2-47c3-bc32-94786d8fc91b ^
    -DICMG_NO_PCH=ON ^
    -DICMG_USE_ONNX=OFF ^
    -DICMG_USE_TREESITTER=OFF ^
    -DICMG_USE_LLAMA=OFF ^
    2>&1
