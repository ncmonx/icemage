@echo off
setlocal
REM v1.43+: NO wipe path. CMake handles delta config automatically.
REM Wipe manually only when generator/compiler changes:  rmdir /s /q build-msvc-full
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0.."
cmake -B build-msvc-full -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=cl ^
    -DCMAKE_CXX_COMPILER=cl ^
    -DICMG_USE_MODULES=OFF ^
    -DICMG_NO_PCH=ON ^
    -DICMG_USE_ONNX=ON ^
    -DICMG_USE_TREESITTER=ON ^
    -DICMG_USE_LLAMA=ON ^
    -DICMG_LLAMA_VULKAN=ON ^
    -DVulkan_INCLUDE_DIR=C:/Users/Administrator/vulkan-headers/include ^
    -DVulkan_LIBRARY=C:/msys64/mingw64/lib/vulkan-1.lib ^
    -DGLSLC_EXECUTABLE=C:/msys64/mingw64/bin/glslc.exe ^
    2>&1
