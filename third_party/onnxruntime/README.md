# ONNX Runtime — vendored C API headers

Headers from [ONNX Runtime v1.25.1](https://github.com/microsoft/onnxruntime/releases/tag/v1.25.1)
official Windows x64 release.

## Why vendor only the header?

- DLL is 14.7 MB — too big for git, and licensed under MIT (can ship in releases).
- C API is ABI-stable across MSVC/MinGW; only need DLL at runtime.
- `cmake -DICMG_USE_ONNX=ON` will auto-download the DLL on first build.

## Manual install (if auto-download fails)

```bash
curl -L -o ort.zip https://github.com/microsoft/onnxruntime/releases/download/v1.25.1/onnxruntime-win-x64-1.25.1.zip
unzip -j ort.zip onnxruntime-win-x64-1.25.1/lib/onnxruntime.dll \
                 onnxruntime-win-x64-1.25.1/lib/onnxruntime_providers_shared.dll \
                 -d third_party/onnxruntime/lib/
```

## License

ONNX Runtime is MIT-licensed. See `LICENSE` in the upstream tarball.
