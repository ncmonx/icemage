@echo off
setlocal
set ICMG_SKIP_RC=1
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "D:\Data Kerja\Personal\AI\icm-graph"
cmake -B build-msvc-full -DCMAKE_BUILD_TYPE=Release -DICMG_USE_LLAMA=ON -DICMG_USE_ONNX=ON -DICMG_USE_TREESITTER=ON -DGGML_VULKAN=ON 2>&1
cmake --build build-msvc-full --target icmg --config Release --parallel 2>&1
