# Building icmg from source

icmg is a single C++17 binary built with CMake. The build links a few native
libraries (ONNX Runtime, tree-sitter, llama.cpp + Vulkan, SQLCipher/OpenSSL).
This guide covers Windows, Linux, and macOS.

## Prerequisites (all platforms)

- **CMake** 3.23+
- **Ninja**
- A **C++17** compiler (MSVC 19.3x / GCC 11+ / Clang 14+)

Optional backends (default ON in the shipped presets):

- **ONNX Runtime** — semantic embeddings (`ICMG_USE_ONNX`)
- **tree-sitter** — symbol extraction (`ICMG_USE_TREESITTER`)
- **llama.cpp + Vulkan** — local LLM (`ICMG_USE_LLAMA`, `ICMG_LLAMA_VULKAN`)
- **SQLCipher + OpenSSL** — encryption at rest (`ICMG_USE_ENCRYPTION`)

---

## Windows (MSVC + MSYS2)

The Windows preset (`msvc-release`) reads three environment variables so the
build is portable across machines. Set them once to point at your local SDKs:

| Env var                  | Points to                                  | Typical value                                  |
| ------------------------ | ------------------------------------------ | ---------------------------------------------- |
| `VULKAN_HEADERS_INCLUDE` | Vulkan headers `include/` dir              | `C:/VulkanSDK/<ver>/Include`                    |
| `ICMG_VCPKG_ROOT`        | vcpkg install root (OpenSSL via vcpkg)     | `C:/vcpkg`                                      |
| `MSYS2_ROOT`             | MSYS2 install root (glslc + vulkan-1.lib)  | `C:/msys64`                                     |

> `ICMG_VCPKG_ROOT` is named distinctly (not `VCPKG_ROOT`) on purpose — the
> Visual Studio developer shell sets its own bundled `VCPKG_ROOT`, which would
> otherwise point the build at the wrong vcpkg tree.

Set them persistently (PowerShell, once):

```powershell
[Environment]::SetEnvironmentVariable('VULKAN_HEADERS_INCLUDE', 'C:/VulkanSDK/1.3.x/Include', 'User')
[Environment]::SetEnvironmentVariable('ICMG_VCPKG_ROOT', 'C:/vcpkg', 'User')
[Environment]::SetEnvironmentVariable('MSYS2_ROOT', 'C:/msys64', 'User')
```

Then build with the project script (PowerShell 7 / `pwsh`):

```powershell
pwsh -File build.ps1                  # build icmg.exe (default)
pwsh -File build.ps1 -Target both -RunTests   # build + run the test suite
pwsh -File build.ps1 -Reconfigure     # safe reconfigure (keeps third_party cache)
```

Or invoke CMake directly with the preset:

```powershell
cmake -B build --preset msvc-release
cmake --build build --parallel
```

### Compiler cache (optional, recommended)

The build auto-detects **sccache** (Mozilla, MSVC-native) on `PATH` or at
`C:/Tools/sccache`, and falls back to **ccache** if present. With a warm cache,
incremental rebuilds are 50–80% faster. Install sccache:

```powershell
winget install Mozilla.sccache
```

---

## Linux

```bash
cmake --preset linux-native
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Install backends via your package manager (Debian/Ubuntu example):

```bash
sudo apt install cmake ninja-build build-essential \
  libssl-dev vulkan-tools libvulkan-dev glslc
```

ONNX Runtime and tree-sitter are vendored under `third_party/`.

---

## macOS

```bash
cmake --preset linux-native     # the generic Unix preset works on macOS
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

```bash
brew install cmake ninja openssl
```

Vulkan is optional on macOS (MoltenVK); to skip the local-LLM Vulkan backend,
configure with `-DICMG_LLAMA_VULKAN=OFF`.

---

## Minimal build (no optional backends)

For the smallest, dependency-light build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DICMG_USE_ONNX=OFF -DICMG_USE_TREESITTER=OFF -DICMG_USE_LLAMA=OFF
cmake --build build --parallel
```

You lose semantic recall, symbol extraction, and the local LLM — but the core
graph, memory, hooks, and MCP server all still work.

---

## Running the tests

```bash
ctest --test-dir build --output-on-failure         # full suite
ctest --test-dir build -R test_curl_bin            # single test by name
./build/test_<name>                                # run a test binary directly
```

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `Could NOT find Vulkan (missing: Vulkan_INCLUDE_DIR)` | `VULKAN_HEADERS_INCLUDE` unset or wrong — point it at the Vulkan `Include/` dir |
| `libcrypto.lib ... missing` (Windows) | `ICMG_VCPKG_ROOT` points at the wrong vcpkg; ensure OpenSSL is installed there |
| `glslc not found` | Install the Vulkan SDK / MSYS2 `mingw-w64-x86_64-shaderc`; check `MSYS2_ROOT` |
| Slow rebuilds | Install sccache (see above) |
