@echo off
REM v1.43+ MSVC compat: generate onnxruntime.lib import library from onnxruntime.dll.
REM MinGW ld can link against .dll direct, MSVC link.exe cannot — needs .lib.
REM
REM Procedure: dumpbin /exports → parse → write .def → lib /def /out:.lib
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "D:\Data Kerja\Personal\AI\icm-graph\third_party\onnxruntime\lib"

echo ==^> Extracting exports from onnxruntime.dll ...
dumpbin /exports onnxruntime.dll > onnxruntime.exports.txt
if errorlevel 1 (
    echo dumpbin failed.
    exit /b 1
)

echo ==^> Generating onnxruntime.def ...
echo LIBRARY onnxruntime > onnxruntime.def
echo EXPORTS >> onnxruntime.def
REM dumpbin export rows: "  <ord>   <hint>  <RVA>   <symbol>"
REM Filter lines with 4 columns, hex-only first 3.
powershell -NoProfile -Command "Get-Content onnxruntime.exports.txt | Where-Object { $_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)' } | ForEach-Object { $matches[1] } | Sort-Object -Unique" >> onnxruntime.def

echo ==^> Building onnxruntime.lib ...
lib /def:onnxruntime.def /out:onnxruntime.lib /machine:x64
if errorlevel 1 (
    echo lib /def failed.
    exit /b 1
)

echo ==^> Done. Output: onnxruntime.lib
dir onnxruntime.lib
