cmake_minimum_required(VERSION 3.28)

# v1.41.0 REAL Modules pilot. Experimental `import std` feature requires CMake
# opted-in via UUID key BEFORE project() call. clang64 (libc++) ships std.cppm
# Modules-capable on Win. MSYS2 mingw64 (libstdc++) NOT yet.
# Set UNCONDITIONAL before project() â€” harmless when ICMG_USE_MODULES=OFF.
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
project(icmg VERSION 2.2.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)  # v1.40.0 M1+: bumped 20 -> 23 per phased plan  # v1.40.0 M1: bumped from 17 â€” enables std::format/expected/concepts/ranges
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Build-speed (v0.53.3): auto-detect ccache + lld, opt-in unity build.
# ---------------------------------------------------------------------------
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    # Smoke-test: ccache --version. Some MSYS2 builds ship with ABI-broken
    # ccache.exe (exit 127 on launch). Only enable if it actually runs.
    execute_process(COMMAND ${CCACHE_PROGRAM} --version
                    RESULT_VARIABLE _ccache_rc
                    OUTPUT_QUIET ERROR_QUIET)
    if(_ccache_rc EQUAL 0)
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
        message(STATUS "icmg: ccache enabled (${CCACHE_PROGRAM}) - incremental builds faster")
    else()
        message(STATUS "icmg: ccache found but smoke-test failed (rc=${_ccache_rc}); skipped")
    endif()
endif()

# Compiler-cache: sccache PRIMARY (Mozilla, MSVC-native, persistent C:/Tools/sccache).
# ccache fallback only if sccache absent (ccache MSYS2 often ABI-broken: 0xC0000139).
if(NOT CMAKE_C_COMPILER_LAUNCHER)
    find_program(SCCACHE_PROGRAM sccache
        HINTS
            "C:/Tools/sccache"
            "$ENV{LOCALAPPDATA}/Microsoft/WinGet/Links"
        PATHS
            "C:/Tools/sccache"
    )
    if(SCCACHE_PROGRAM)
        execute_process(COMMAND "${SCCACHE_PROGRAM}" --version
                        RESULT_VARIABLE _sc_rc OUTPUT_QUIET ERROR_QUIET)
        if(_sc_rc EQUAL 0)
            set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
            set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
            message(STATUS "icmg: sccache enabled (${SCCACHE_PROGRAM}) - persistent compiler cache")
        endif()
    endif()
endif()

# Opt-in lld linker (~30-50% link phase faster). Off by default because
# MinGW+ONNX direct-DLL-link uses MS export tables lld handles differently
# (link rc=57). Enable: cmake -B build -DICMG_USE_LLD=ON
option(ICMG_USE_LLD "Use lld linker instead of ld (faster but breaks MinGW+ONNX direct-DLL link)" OFF)
if(ICMG_USE_LLD)
    find_program(LLD_PROGRAM ld.lld)
    if(LLD_PROGRAM AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_link_options(-fuse-ld=lld)
        message(STATUS "icmg: lld linker enabled (${LLD_PROGRAM}) - link phase faster")
    else()
        message(WARNING "icmg: ICMG_USE_LLD=ON but ld.lld not found; falling back to default linker")
    endif()
endif()

# Opt-in unity build (merges .cpp into batches; faster compile, occasional
# ODR collision risk on file-static names). Enable: cmake -B build -DICMG_UNITY_BUILD=ON
option(ICMG_UNITY_BUILD "Merge .cpp files into batches for faster compilation" OFF)

# v1.41.0: C++23 Modules opt-in scaffold. Default OFF â€” actual `import std;`
# conversion deferred until GCC + libstdc++ ship stable Modules support for
# template-heavy code. When ON, future v1.41.x will emit FILE_SET CXX_MODULES.
option(ICMG_USE_MODULES "Enable C++23 import std; modules pilot (experimental)" OFF)
if(ICMG_USE_MODULES)
    # v1.41.0: per-target opt-in (avoid global CMAKE_CXX_MODULE_STD which
    # propagates to C-only sqlite3). icmg_lib gets the property below.
    add_compile_definitions(ICMG_USE_MODULES_PILOT=1)
    message(STATUS "icmg: ICMG_USE_MODULES=ON â€” REAL Modules pilot (persona_cmd uses import std;)")
endif()

# ---------------------------------------------------------------------------
# Third-party: SQLite3 amalgamation (auto-downloaded if missing ï¿½ gitignored)
# ---------------------------------------------------------------------------
set(SQLITE_AMALGAMATION_VERSION "3470100")  # 3.47.1, year 2024
set(SQLITE_AMALGAMATION_ZIP "sqlite-amalgamation-${SQLITE_AMALGAMATION_VERSION}.zip")
set(SQLITE_AMALGAMATION_URL  "https://www.sqlite.org/2024/${SQLITE_AMALGAMATION_ZIP}")
set(SQLITE_DIR ${CMAKE_SOURCE_DIR}/third_party/sqlite3)
if(NOT EXISTS ${SQLITE_DIR}/sqlite3.c)
    message(STATUS "icmg: fetching sqlite3 amalgamation ${SQLITE_AMALGAMATION_VERSION} ...")
    file(MAKE_DIRECTORY ${SQLITE_DIR})
    file(DOWNLOAD ${SQLITE_AMALGAMATION_URL} ${CMAKE_BINARY_DIR}/_sqlite.zip
         SHOW_PROGRESS STATUS _sqlite_status)
    list(GET _sqlite_status 0 _sqlite_rc)
    if(_sqlite_rc EQUAL 0)
        file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/_sqlite.zip
             DESTINATION ${CMAKE_BINARY_DIR}/_sqlite_unpack)
        file(GLOB _sqlite_amalg
             "${CMAKE_BINARY_DIR}/_sqlite_unpack/sqlite-amalgamation-*/sqlite3.[ch]")
        foreach(_f ${_sqlite_amalg})
            file(COPY ${_f} DESTINATION ${SQLITE_DIR})
        endforeach()
        file(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/_sqlite_unpack ${CMAKE_BINARY_DIR}/_sqlite.zip)
    else()
        message(FATAL_ERROR "sqlite3 amalgamation download failed; see ${SQLITE_AMALGAMATION_URL}")
    endif()
endif()

# nlohmann/json single-header (gitignored ï¿½ auto-fetch if missing)
set(NLOHMANN_JSON_VERSION "3.11.3")
set(NLOHMANN_JSON_URL "https://github.com/nlohmann/json/releases/download/v${NLOHMANN_JSON_VERSION}/json.hpp")
set(NLOHMANN_DIR ${CMAKE_SOURCE_DIR}/third_party/nlohmann)
if(NOT EXISTS ${NLOHMANN_DIR}/json.hpp)
    message(STATUS "icmg: fetching nlohmann/json v${NLOHMANN_JSON_VERSION} ...")
    file(MAKE_DIRECTORY ${NLOHMANN_DIR})
    file(DOWNLOAD ${NLOHMANN_JSON_URL} ${NLOHMANN_DIR}/json.hpp
         STATUS _nj_status)
    list(GET _nj_status 0 _nj_rc)
    if(NOT _nj_rc EQUAL 0)
        message(FATAL_ERROR "nlohmann/json download failed; fetch manually")
    endif()
endif()

# v1.76: encryption-at-rest. When ICMG_USE_ENCRYPTION=ON (default), build the
# `sqlite3` target from the vendored SQLCipher amalgamation with the Windows
# CNG crypto provider (BCrypt â€” no new DLL; bcrypt/crypt32 are system libs).
# SQLCipher without a PRAGMA key behaves exactly like plain SQLite, so existing
# plaintext DBs keep working; encryption is runtime opt-in (icmg encrypt enable).
option(ICMG_USE_ENCRYPTION "Build sqlite3 from SQLCipher (encryption-at-rest capable)" ON)
if(ICMG_USE_ENCRYPTION)
    add_library(sqlite3 STATIC third_party/sqlcipher/sqlite3.c)
    target_include_directories(sqlite3 PUBLIC third_party/sqlcipher)
    target_compile_definitions(sqlite3 PUBLIC
        SQLITE_THREADSAFE=1
        SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
        SQLITE_ENABLE_FTS5=1
        SQLITE_DEFAULT_MEMSTATUS=0
        SQLITE_OMIT_DEPRECATED
        SQLITE_OMIT_PROGRESS_CALLBACK
        SQLITE_HAS_CODEC
        SQLITE_TEMP_STORE=2
        # SQLCipher mandates these init/shutdown hooks be registered.
        SQLITE_EXTRA_INIT=sqlcipher_extra_init
        SQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown)
    # Crypto provider = OpenSSL (SQLCipher 3.53 has no CNG provider). On Windows
    # we link the vcpkg-built MSVC OpenSSL (libcrypto) and bundle libcrypto-3-x64.dll.
    target_compile_definitions(sqlite3 PUBLIC SQLCIPHER_CRYPTO_OPENSSL)
    if(MSVC)
        # MSVC: link vcpkg-built OpenSSL import lib + bundle libcrypto-3-x64.dll.
        set(ICMG_VCPKG_ROOT "C:/vcpkg/installed/x64-windows" CACHE PATH "vcpkg x64-windows tree (OpenSSL)")
        target_include_directories(sqlite3 PUBLIC ${ICMG_VCPKG_ROOT}/include)
        target_link_libraries(sqlite3 PUBLIC
            ${ICMG_VCPKG_ROOT}/lib/libcrypto.lib
            ws2_32 crypt32)
    elseif(WIN32)
        # MinGW/MSYS2: system OpenSSL from the toolchain (find_package or -lcrypto).
        find_package(OpenSSL QUIET)
        if(OpenSSL_FOUND)
            target_link_libraries(sqlite3 PUBLIC OpenSSL::Crypto ws2_32 crypt32)
        else()
            target_link_libraries(sqlite3 PUBLIC crypto ws2_32 crypt32)
        endif()
    else()
        # Linux/macOS: find OpenSSL properly (macOS brew puts it in a non-standard
        # prefix; bare -lcrypto misses the headers).
        find_package(OpenSSL REQUIRED)
        target_link_libraries(sqlite3 PUBLIC OpenSSL::Crypto)
    endif()
    message(STATUS "icmg: SQLCipher encryption-at-rest ENABLED (OpenSSL provider)")
else()
    add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
    target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
    target_compile_definitions(sqlite3 PUBLIC
        SQLITE_THREADSAFE=1
        SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
        SQLITE_ENABLE_FTS5=1
        SQLITE_DEFAULT_MEMSTATUS=0
        SQLITE_OMIT_DEPRECATED
        SQLITE_OMIT_PROGRESS_CALLBACK
    )
endif()

# Third-party: nlohmann/json single header
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party)

# ---------------------------------------------------------------------------
# icmg_lib â€” all src/ except main.cpp (shared by binary + tests)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE ALL_SOURCES "src/*.cpp")
list(FILTER ALL_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_library(icmg_lib STATIC ${ALL_SOURCES})
target_include_directories(icmg_lib PUBLIC src third_party third_party/wasmtime/include)
target_link_libraries(icmg_lib PUBLIC sqlite3 nlohmann_json)
if(ICMG_USE_MODULES)
    # v1.41.0: per-target Modules opt-in (avoids breaking C-only sqlite3).
    set_target_properties(icmg_lib PROPERTIES CXX_MODULE_STD ON)
    message(STATUS "icmg: icmg_lib uses import std; (clang64 + libc++)")
endif()

# v1.26.0 (P1): precompiled headers. The 5 biggest hitters by parse cost
# across ~250 TUs â€” `<nlohmann/json.hpp>` alone is ~26 k LOC and appears in
# ~70% of icmg_lib sources. PCH cuts cold-compile phase ~10-20%. Opt-out
# via -DICMG_NO_PCH=ON when PCH-induced parse errors surface on exotic
# toolchains (rare).
option(ICMG_NO_PCH "Disable precompiled headers (debug exotic compile errors)" OFF)
if(NOT ICMG_NO_PCH)
    target_precompile_headers(icmg_lib PRIVATE
        <nlohmann/json.hpp>
        <string> <vector> <unordered_map> <unordered_set>
        <memory> <filesystem> <fstream> <sstream>
        <chrono> <optional>
        # v1.31.0: extend hot-header set. iostream in ~80%% of TUs;
        # algorithm + cstdint in ~50%%. Adds ~5-10 MB PCH but cuts
        # parse phase another ~10%%.
        <iostream> <algorithm> <cstdint> <cstddef>
        <map> <set> <array> <functional>
    )
    message(STATUS "icmg: PCH enabled for icmg_lib (~10-20% cold-compile saving)")
endif()

# Phase build-speed (v0.53.3): unity build opt-in. Compiles 8 .cpp at a time
# in single TU - reduces front-end parse work ~20-40%. May expose ODR
# collisions on file-static names; if build fails, set -DICMG_UNITY_BUILD=OFF.
if(ICMG_UNITY_BUILD)
    set_target_properties(icmg_lib PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 8)
    message(STATUS "icmg: unity build enabled for icmg_lib (batch=8)")
endif()

if(WIN32)
    target_link_libraries(icmg_lib PUBLIC ws2_32 shlwapi bcrypt crypt32)
    target_compile_definitions(icmg_lib PUBLIC
        _WIN32_WINNT=0x0601 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(UNIX)
    target_link_libraries(icmg_lib PUBLIC pthread dl)
endif()

# ---------------------------------------------------------------------------
# Phase 33 â€” optional backends. Both default OFF; system-detected on opt-in.
# ---------------------------------------------------------------------------
option(ICMG_USE_ONNX        "Build ONNX Runtime embedder backend (Phase 33+)" OFF)
option(ICMG_USE_TREESITTER  "Build tree-sitter AST symbol extractor (Phase 33+)" OFF)

if(ICMG_USE_ONNX)
    set(ORT_INCLUDE ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/include)
    set(ORT_LIB_DIR ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/lib)
    set(ORT_VERSION "1.25.1")

    # v0.58.0 (Phase 84 T2): per-OS archive + library naming.
    if(WIN32)
        set(ORT_ARCHIVE "onnxruntime-win-x64-${ORT_VERSION}.zip")
        # v1.43+ MSVC link needs .lib import library (gen via scripts/gen-onnxruntime-lib.bat).
        # MinGW ld can link against .dll directly â€” keeps existing behaviour.
        if(MSVC)
            set(ORT_LIB_NAME "onnxruntime.lib")
        else()
            set(ORT_LIB_NAME "onnxruntime.dll")
        endif()
        set(ORT_LIB_GLOB "*.dll")
    elseif(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(ORT_ARCHIVE "onnxruntime-osx-arm64-${ORT_VERSION}.tgz")
        else()
            set(ORT_ARCHIVE "onnxruntime-osx-x86_64-${ORT_VERSION}.tgz")
        endif()
        set(ORT_LIB_NAME "libonnxruntime.${ORT_VERSION}.dylib")
        set(ORT_LIB_GLOB "*.dylib")
    elseif(UNIX)
        set(ORT_ARCHIVE "onnxruntime-linux-x64-${ORT_VERSION}.tgz")
        set(ORT_LIB_NAME "libonnxruntime.so.${ORT_VERSION}")
        set(ORT_LIB_GLOB "*.so*")
    endif()
    set(ORT_LIB ${ORT_LIB_DIR}/${ORT_LIB_NAME})
    set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_ARCHIVE}")

    # Auto-download lib if missing (header is already vendored).
    if(NOT EXISTS ${ORT_LIB})
        message(STATUS "icmg: fetching onnxruntime ${ORT_VERSION} for ${CMAKE_SYSTEM_NAME} (one-time, ~75MB)...")
        file(DOWNLOAD ${ORT_URL} ${CMAKE_BINARY_DIR}/_ort_archive
             SHOW_PROGRESS STATUS _ort_status)
        list(GET _ort_status 0 _ort_rc)
        if(_ort_rc EQUAL 0)
            file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/_ort_unpack)
            file(MAKE_DIRECTORY ${ORT_LIB_DIR})
            if(UNIX)
                # v0.59.0: POSIX uses tar + cp -R so symlinks (.dylib / .so chain)
                # are preserved. CMake file(ARCHIVE_EXTRACT) + file(COPY)
                # dereference them ï¿½ produced "unsupported mach-o filetype" on
                # macOS because the .X.Y.Z.dylib alias became broken.
                execute_process(
                    COMMAND tar -xzf ${CMAKE_BINARY_DIR}/_ort_archive
                            -C ${CMAKE_BINARY_DIR}/_ort_unpack
                    RESULT_VARIABLE _ort_extract_rc)
                if(_ort_extract_rc EQUAL 0)
                    # Copy lib/ contents preserving symlinks. -P (do not follow)
                    # on macOS BSD cp; -P also accepted on GNU cp.
                    execute_process(
                        COMMAND bash -c "cp -RP ${CMAKE_BINARY_DIR}/_ort_unpack/*/lib/* ${ORT_LIB_DIR}/")
                endif()
            else()
                # Windows .zip ï¿½ no symlinks in ORT-Win archive.
                file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/_ort_archive
                     DESTINATION ${CMAKE_BINARY_DIR}/_ort_unpack)
                file(GLOB_RECURSE _ort_libs
                     "${CMAKE_BINARY_DIR}/_ort_unpack/**/lib/${ORT_LIB_GLOB}")
                foreach(_d ${_ort_libs})
                    file(COPY ${_d} DESTINATION ${ORT_LIB_DIR})
                endforeach()
            endif()
            file(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/_ort_unpack ${CMAKE_BINARY_DIR}/_ort_archive)
        else()
            message(WARNING "icmg: ORT auto-download failed (${_ort_status}); "
                    "fetch manually per third_party/onnxruntime/README.md")
        endif()
    endif()

    if(EXISTS ${ORT_LIB} AND EXISTS ${ORT_INCLUDE}/onnxruntime_c_api.h)
        message(STATUS "icmg: ONNX backend ENABLED (lib: ${ORT_LIB})")
        target_compile_definitions(icmg_lib PUBLIC ICMG_HAS_ONNX=1)
        target_include_directories(icmg_lib PUBLIC ${ORT_INCLUDE})
        target_link_libraries(icmg_lib PUBLIC ${ORT_LIB})
        # Bundle runtime libs next to executable.
        file(GLOB _ort_runtime_libs "${ORT_LIB_DIR}/${ORT_LIB_GLOB}")
        foreach(_d ${_ort_runtime_libs})
            file(COPY ${_d} DESTINATION ${CMAKE_BINARY_DIR})
        endforeach()
        if(UNIX AND NOT APPLE)
            # Linux runtime path so the binary finds libonnxruntime.so.X at exec.
            set_target_properties(icmg_lib PROPERTIES
                INSTALL_RPATH "$ORIGIN"
                BUILD_RPATH   "$ORIGIN")
        elseif(APPLE)
            set_target_properties(icmg_lib PROPERTIES
                INSTALL_RPATH "@loader_path"
                BUILD_RPATH   "@loader_path")
        endif()
    else()
        message(WARNING "icmg: ICMG_USE_ONNX=ON but ORT lib/header not found "
                "(lib=${ORT_LIB}, header=${ORT_INCLUDE}). "
                "Build proceeds without ONNX backend. "
                "See third_party/onnxruntime/README.md for manual install.")
    endif()
endif()

if(ICMG_USE_TREESITTER)
    # v0.59.0: vendor libtree-sitter source. Previously used find_library which
    # picked up apt's v0.20.x on Linux ï¿½ ABI-incompatible with v0.26 vendored
    # grammars (silent empty parse trees). Building from source pinned to v0.24.5
    # guarantees grammar-runtime ABI match (parser.h v14).
    # Vendored grammars (parser.c LANGUAGE_VERSION 15) require runtime = v0.25.
    # v0.26.x is current stable line; matches MSYS2 mingw-w64-x86_64-tree-sitter.
    set(TS_RUNTIME_VERSION "0.26.0")
    set(TS_RUNTIME_URL
        "https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v${TS_RUNTIME_VERSION}.tar.gz")
    set(TS_RUNTIME_DIR ${CMAKE_SOURCE_DIR}/third_party/tree-sitter-lib)
    if(NOT EXISTS ${TS_RUNTIME_DIR}/lib/src/lib.c)
        message(STATUS "icmg: fetching libtree-sitter ${TS_RUNTIME_VERSION} (one-time, ~1MB)...")
        file(MAKE_DIRECTORY ${TS_RUNTIME_DIR})
        # CMake file(DOWNLOAD) follow_redirect handling is patchy on some
        # systems (TLS cert chain, redirect to codeload.github.com). Use curl
        # via execute_process ï¿½ universally available on CI runners (preinstalled
        # on Win/Linux/macOS) and on MSYS2 dev workstations.
        # -k bypasses local CA-bundle errors (broken cert chain on some MSYS2
        # installs). Source integrity is verified by tarball content + grammar
        # ABI checks at build time, not by TLS chain at download time.
        execute_process(
            COMMAND curl -fsSLk -o ${CMAKE_BINARY_DIR}/_ts.tar.gz ${TS_RUNTIME_URL}
            RESULT_VARIABLE _ts_rc)
        if(_ts_rc EQUAL 0)
            file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/_ts_unpack)
            file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/_ts.tar.gz
                 DESTINATION ${CMAKE_BINARY_DIR}/_ts_unpack)
            # tarball root is tree-sitter-<version>/ ; move its lib/ into our vendor dir.
            file(GLOB _ts_src_dirs "${CMAKE_BINARY_DIR}/_ts_unpack/tree-sitter-*")
            foreach(_d ${_ts_src_dirs})
                if(IS_DIRECTORY ${_d}/lib)
                    file(COPY ${_d}/lib DESTINATION ${TS_RUNTIME_DIR})
                endif()
            endforeach()
            file(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/_ts_unpack ${CMAKE_BINARY_DIR}/_ts.tar.gz)
        else()
            message(FATAL_ERROR "libtree-sitter download failed (curl rc=${_ts_rc}); fetch manually from ${TS_RUNTIME_URL}")
        endif()
    endif()

    # Build vendored runtime as static lib (single-TU unity via lib.c).
    add_library(tree_sitter_runtime STATIC ${TS_RUNTIME_DIR}/lib/src/lib.c)
    target_include_directories(tree_sitter_runtime PUBLIC
        ${TS_RUNTIME_DIR}/lib/include   # tree_sitter/api.h (public)
        ${TS_RUNTIME_DIR}/lib/src)      # internals for lib.c's #includes
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(tree_sitter_runtime PRIVATE -w -std=c11)
    endif()
    # glibc gates be16toh/le16toh (UTF-16 decode in tree-sitter v0.25+ lib.c)
    # behind _GNU_SOURCE / _DEFAULT_SOURCE feature macro. macOS libc + MinGW
    # expose them unconditionally so guard the define to Linux only.
    if(UNIX AND NOT APPLE)
        target_compile_definitions(tree_sitter_runtime PRIVATE _GNU_SOURCE)
    endif()

    set(TS_INCLUDE ${TS_RUNTIME_DIR}/lib/include)
    set(TS_LIB tree_sitter_runtime)
    set(TS_C_GRAMMAR ${CMAKE_SOURCE_DIR}/third_party/tree-sitter-c/src/parser.c)
    if(EXISTS ${TS_C_GRAMMAR})
        message(STATUS "icmg: tree-sitter ENABLED (vendored runtime v${TS_RUNTIME_VERSION}, C grammar vendored)")
        # Vendored grammar â€” pure C, no extra deps.
        add_library(tree_sitter_c STATIC ${TS_C_GRAMMAR})
        target_include_directories(tree_sitter_c PRIVATE
            ${CMAKE_SOURCE_DIR}/third_party/tree-sitter-c/src
            ${TS_INCLUDE})
        if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(tree_sitter_c PRIVATE -w)  # silence grammar warnings
        endif()

        target_compile_definitions(icmg_lib PUBLIC ICMG_HAS_TREESITTER=1)
        target_include_directories(icmg_lib PUBLIC ${TS_INCLUDE})
        target_link_libraries(icmg_lib PUBLIC tree_sitter_c ${TS_LIB})

        # Phase 40 T3: tree-sitter-typescript grammar (vendored).
        set(TS_TS_DIR ${CMAKE_SOURCE_DIR}/third_party/tree-sitter-typescript/src)
        if(EXISTS ${TS_TS_DIR}/parser.c)
            add_library(tree_sitter_typescript STATIC
                ${TS_TS_DIR}/parser.c
                ${TS_TS_DIR}/scanner.c)
            target_include_directories(tree_sitter_typescript PRIVATE
                ${TS_TS_DIR} ${TS_INCLUDE})
            if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
                target_compile_options(tree_sitter_typescript PRIVATE -w)
            endif()
            target_compile_definitions(icmg_lib PUBLIC ICMG_HAS_TREESITTER_TS=1)
            target_link_libraries(icmg_lib PUBLIC tree_sitter_typescript)
            message(STATUS "icmg: tree-sitter-typescript ENABLED")
        endif()

        # Phase 40 T4: tree-sitter-python grammar (vendored).
        set(TS_PY_DIR ${CMAKE_SOURCE_DIR}/third_party/tree-sitter-python/src)
        if(EXISTS ${TS_PY_DIR}/parser.c)
            add_library(tree_sitter_python STATIC
                ${TS_PY_DIR}/parser.c
                ${TS_PY_DIR}/scanner.c)
            target_include_directories(tree_sitter_python PRIVATE
                ${TS_PY_DIR} ${TS_INCLUDE})
            if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
                target_compile_options(tree_sitter_python PRIVATE -w)
            endif()
            target_compile_definitions(icmg_lib PUBLIC ICMG_HAS_TREESITTER_PY=1)
            target_link_libraries(icmg_lib PUBLIC tree_sitter_python)
            message(STATUS "icmg: tree-sitter-python ENABLED")
        endif()

        # v1.27.0 Phase 3: tree-sitter-vue/html/svelte grammars (vendored in-tree).
        # tag.h scanner needs `tree_sitter/array.h` from runtime lib/src dir.
        set(_TS_RUNTIME_SRC ${TS_RUNTIME_DIR}/lib/src)
        foreach(_TS_LANG vue html svelte go rust java cpp lua dart)
            set(_TS_DIR ${CMAKE_SOURCE_DIR}/third_party/tree-sitter-${_TS_LANG}/src)
            if(EXISTS ${_TS_DIR}/parser.c)
                set(_TS_SRC ${_TS_DIR}/parser.c)
                if(EXISTS ${_TS_DIR}/scanner.c)
                    list(APPEND _TS_SRC ${_TS_DIR}/scanner.c)
                endif()
                add_library(tree_sitter_${_TS_LANG} STATIC ${_TS_SRC})
                target_include_directories(tree_sitter_${_TS_LANG} PRIVATE
                    ${_TS_DIR} ${TS_INCLUDE} ${_TS_RUNTIME_SRC})
                if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
                    target_compile_options(tree_sitter_${_TS_LANG} PRIVATE -w)
                endif()
                string(TOUPPER ${_TS_LANG} _TS_UPPER)
                target_compile_definitions(icmg_lib PUBLIC ICMG_HAS_TREESITTER_${_TS_UPPER}=1)
                target_link_libraries(icmg_lib PUBLIC tree_sitter_${_TS_LANG})
                message(STATUS "icmg: tree-sitter-${_TS_LANG} ENABLED")
            endif()
        endforeach()

        # v0.59.0: vendored libtree-sitter is static ï¿½ no more libtree-sitter*.dll
        # / wasmtime.dll / libzstd.dll runtime dependency. libwinpthread is still
        # needed (std::thread on MinGW posix-thread model) UNLESS the binary is
        # statically linked, which icmg.exe does via target_link_options below.
        # Keep this block as a safety copy in case static-libstdc++ doesn't pull
        # winpthread cleanly on some MinGW builds.
        if(WIN32)
            set(TS_DLL_NAMES libwinpthread-1.dll)
            foreach(_dll IN LISTS TS_DLL_NAMES)
                find_file(TS_DLL_${_dll} NAMES ${_dll}
                          PATHS /c/msys64/mingw64/bin /mingw64/bin)
                if(TS_DLL_${_dll})
                    message(STATUS "icmg: copy DLL ${_dll}")
                    if(NOT EXISTS "${CMAKE_BINARY_DIR}/${_dll}")
                        file(COPY ${TS_DLL_${_dll}} DESTINATION ${CMAKE_BINARY_DIR})
                    endif()
                endif()
            endforeach()
        endif()
    else()
        message(WARNING "icmg: ICMG_USE_TREESITTER=ON but tree-sitter lib/headers/grammar "
                "not found (lib=${TS_LIB} include=${TS_INCLUDE} grammar=${TS_C_GRAMMAR}). "
                "Falling back to regex extractor.")
    endif()
endif()

# Warnings on icmg_lib
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(icmg_lib PRIVATE -Wall -Wextra -Wno-unused-parameter)
elseif(MSVC)
    target_compile_options(icmg_lib PRIVATE /W4 /wd4100)
endif()

# ---------------------------------------------------------------------------
# icmg executable
# --whole-archive: ensures static-init ICMG_REGISTER_* macros are not
# dead-stripped (same pattern as test binaries).
# ---------------------------------------------------------------------------
if(WIN32 AND NOT DEFINED ENV{ICMG_SKIP_RC})
    enable_language(RC)
    set(ICMG_WIN_RESOURCE src/icmg.rc)
else()
    set(ICMG_WIN_RESOURCE)
endif()
add_executable(icmg src/main.cpp ${ICMG_WIN_RESOURCE})

# v1.7.0: minimal launcher stub.
if(WIN32)
    add_executable(icmg-launcher src/exec_client.c)
    target_link_libraries(icmg-launcher PRIVATE kernel32 user32)
    target_link_options(icmg-launcher PRIVATE -s -mwindows)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(APPLE)
        # Apple ld64 doesn't support --whole-archive. Use -force_load on the
        # archive path, and ALSO add icmg_lib as a normal dep so transitive
        # link libs (sqlite3, ORT, etc.) are picked up.
        target_link_libraries(icmg PRIVATE
            -Wl,-force_load,$<TARGET_FILE:icmg_lib>
            icmg_lib)
    else()
        target_link_libraries(icmg PRIVATE
            -Wl,--whole-archive icmg_lib -Wl,--no-whole-archive)
    endif()
elseif(MSVC)
    # v1.41.x MSVC equivalent of --whole-archive. /WHOLEARCHIVE forces all
    # icmg_lib.lib .obj files to be linked even if no symbol is referenced â€”
    # required so static-init ICMG_REGISTER_COMMAND macros pull every cmd in.
    target_link_libraries(icmg PRIVATE icmg_lib)
    target_link_options(icmg PRIVATE "/WHOLEARCHIVE:icmg_lib")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Static link runtime â†’ portable exe (no mingw DLL deps).
    if(WIN32)
        target_link_options(icmg PRIVATE
            -static-libgcc -static-libstdc++
            -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
            # v0.58.1 popup fix: link as GUI subsystem so Windows does NOT
            # auto-allocate a console (no flash on Task Scheduler / Explorer
            # invocation). main() calls AttachConsole(ATTACH_PARENT_PROCESS)
            # to inherit caller's console when run interactively from cmd/bash.
            -mwindows)
    endif()
else()
    target_link_libraries(icmg PRIVATE icmg_lib)
endif()

# v1.41.x MSVC: icmg.rc already embeds RT_MANIFEST (private SxS DLL isolation).
# MSVC linker tries to auto-embed another by default -> CVT1100 duplicate.
# Disable auto-manifest on MSVC; MSYS2 ld is fine.
if(MSVC)
    target_link_options(icmg PRIVATE "/MANIFEST:NO")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(UNIX AND NOT APPLE)
        target_link_options(icmg PRIVATE -s)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

# v1.26.0: REVERTED mono test exe consolidation.
# Mono test attempt caused: state-leak hangs (DB singleton across tests),
# bash.exe bloat (each ctest filter spawn forks shell), filter-stem mismatch
# breaking 62/115 ctest entries. Pragmatic ship: per-exe model retained.
# Build-time wins from PCH (~15%) + PowerShell matcher widen + RAW=1 nag.
# Future mono attempt requires TEST() name audit + DB/service singleton
# teardown between cases â€” out of scope for v1.26.0.
# v2.0 fix: define these BEFORE add_icmg_test runs â€” otherwise on a fresh
# CMake cache the macro sees ICMG_MONO_TEST undefined and wrongly builds the
# per-exe path (195 phantom ctest entries that Not-Run under mono).
option(ICMG_PER_EXE_TEST "Build per-exe tests (one binary per file, ~74 exes) â€” only for isolation debugging" OFF)
option(ICMG_MONO_TEST "Build single icmg_test binary (DEFAULT ON v1.40.0; saves ~2.5 min link + 1.5 GB disk)" ON)
if(ICMG_PER_EXE_TEST)
    set(ICMG_MONO_TEST OFF CACHE BOOL "" FORCE)
    message(STATUS "icmg: ICMG_PER_EXE_TEST=ON â€” disabling mono, building per-exe tests (slow path)")
endif()

macro(add_icmg_test name source_file)
    # v1.40.0: always collect into ICMG_ALL_TEST_SOURCES (mono needs it).
    # When ICMG_MONO_TEST=ON (default), skip per-exe target â€” only mono builds.
    set_property(GLOBAL APPEND PROPERTY ICMG_ALL_TEST_SOURCES ${source_file})
    if(NOT ICMG_MONO_TEST)
        add_executable(${name} ${source_file})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            if(APPLE)
                target_link_libraries(${name} PRIVATE
                    -Wl,-force_load,$<TARGET_FILE:icmg_lib>
                    icmg_lib)
            else()
                target_link_libraries(${name} PRIVATE
                    -Wl,--whole-archive icmg_lib -Wl,--no-whole-archive)
            endif()
            if(WIN32)
                target_link_options(${name} PRIVATE
                    -static-libgcc -static-libstdc++
                    -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
            endif()
        else()
            target_link_libraries(${name} PRIVATE icmg_lib)
        endif()
        target_include_directories(${name} PRIVATE tests src third_party)
        add_test(NAME ${name} COMMAND ${name})
    endif()
endmacro()

add_icmg_test(test_parseargv    tests/tkil/test_parseargv.cpp)
add_icmg_test(test_detector     tests/tkil/test_detector.cpp)
add_icmg_test(test_filter_utils tests/tkil/test_filter_utils.cpp)
add_icmg_test(test_filters      tests/tkil/test_filters.cpp)
add_icmg_test(test_lang_filters tests/tkil/test_lang_filters.cpp)  # v1.21.5
add_icmg_test(test_dedup_pass   tests/tkil/test_dedup_pass.cpp)
add_icmg_test(test_db_mmap      tests/core/test_db_mmap.cpp)
add_icmg_test(test_exec_context tests/core/test_exec_context.cpp)
add_icmg_test(test_smart_router tests/llm/test_smart_router.cpp)
add_icmg_test(test_agent_local  tests/cli/test_agent_local.cpp)
add_icmg_test(test_auto_consolidate tests/imem/test_auto_consolidate.cpp)
add_icmg_test(test_moment_helpers tests/imem/test_moment_helpers.cpp)
add_icmg_test(test_comms_archive  tests/core/test_comms_archive.cpp)
add_icmg_test(test_quick_store    tests/cli/test_quick_store.cpp)
add_icmg_test(test_wire_check     tests/core/test_wire_check.cpp)
add_icmg_test(test_resume         tests/cli/test_resume.cpp)
add_icmg_test(test_layer0_extract tests/imem/test_layer0_extract.cpp)
add_icmg_test(test_entity_extract tests/imem/test_entity_extract.cpp)
add_icmg_test(test_strict_audit   tests/cli/test_strict_audit.cpp)
add_icmg_test(test_service_singleton tests/core/test_service_singleton.cpp)
add_icmg_test(test_wasm_skill    tests/wasm/test_wasm_skill.cpp)
add_icmg_test(test_wasm_runtime tests/wasm/test_wasm_runtime.cpp)
add_icmg_test(test_context_budget tests/cli/test_context_budget.cpp)
add_icmg_test(test_dense_summary tests/cli/test_dense_summary.cpp)  # odysseus-inspired dense compaction summary (AGPL-clean reimpl)
add_icmg_test(test_persona_local_only tests/core/test_persona_local_only.cpp)
add_icmg_test(test_write_queue   tests/core/test_write_queue.cpp)
add_icmg_test(test_node_bloom   tests/graph/test_node_bloom.cpp)
add_icmg_test(test_getnode_bloom tests/graph/test_getnode_bloom.cpp)
add_icmg_test(test_cmd_bigram   tests/core/test_cmd_bigram.cpp)
add_icmg_test(test_persona_db    tests/core/test_persona_db.cpp)
add_icmg_test(test_daemon_glossary tests/server/test_daemon_glossary.cpp)
add_icmg_test(test_relax_acl    tests/core/test_relax_acl.cpp)
add_icmg_test(test_path_safety   tests/core/test_path_safety.cpp)
add_icmg_test(test_token_counter tests/core/test_token_counter.cpp)
add_icmg_test(test_bpe_tokenizer tests/core/test_bpe_tokenizer.cpp)
add_icmg_test(test_presence tests/core/test_presence.cpp)
add_icmg_test(test_event_bus tests/core/test_event_bus.cpp)
add_icmg_test(test_msg tests/core/test_msg.cpp)
add_icmg_test(test_hot tests/core/test_hot.cpp)
add_icmg_test(test_wire_dir tests/core/test_wire_dir.cpp)
add_icmg_test(test_canonicalize_noexcept tests/core/test_canonicalize_noexcept.cpp) # v1.78 #174
add_icmg_test(test_migration_idempotent tests/core/test_migration_idempotent.cpp)
add_icmg_test(test_server_auth   tests/core/test_server_auth.cpp)            # v1.68 S2
add_icmg_test(test_rate_limiter  tests/core/test_rate_limiter.cpp)          # v1.72 Security
add_icmg_test(test_scan_tree     tests/core/test_scan_tree.cpp)              # v1.68 S3
add_icmg_test(test_json_safe     tests/core/test_json_safe.cpp)              # v1.68.1
add_icmg_test(test_run_args      tests/cli/test_run_args.cpp)                # v1.70 #178
add_icmg_test(test_recall_json   tests/cli/test_recall_json.cpp)             # v1.70 #176
add_icmg_test(test_llm_list_json tests/cli/test_llm_list_json.cpp)           # v1.70 #177
add_icmg_test(test_pattern_pass tests/tkil/test_pattern_pass.cpp)
add_icmg_test(test_outcome      tests/tkil/test_outcome.cpp)
add_icmg_test(test_session_glossary tests/tkil/test_session_glossary.cpp)
add_icmg_test(test_ultra_pipeline tests/tkil/test_ultra_pipeline.cpp)
add_icmg_test(test_rpc_protocol tests/server/test_rpc_protocol.cpp)
add_icmg_test(test_daemon_lifecycle tests/server/test_daemon_lifecycle.cpp)    # v1.56 T1-stage2  # v1.21.5 (TDD: F3)
add_icmg_test(test_layout_extractor tests/style_clone/test_layout_extractor.cpp)  # v1.22.0 (SC3)
add_icmg_test(test_db           tests/core/test_db.cpp)
add_icmg_test(test_db_prepared  tests/core/test_db_prepared.cpp)
add_icmg_test(test_scorer       tests/imem/test_scorer.cpp)
add_icmg_test(test_semantic_dedup tests/imem/test_semantic_dedup.cpp)
add_icmg_test(test_importance_decay tests/imem/test_importance_decay.cpp)  # v1.23.0 (TDD: M2)
add_icmg_test(test_atom_store    tests/imem/test_atom_store.cpp)  # v1.79 atomize T1
add_icmg_test(test_memory_source tests/imem/test_memory_source.cpp)  # provenance: memory source (ctor guarded-ALTER)
add_icmg_test(test_atom_split    tests/imem/test_atom_split.cpp)  # v1.79 atomize T2
add_icmg_test(test_bisect_logic  tests/cli/test_bisect_logic.cpp)  # v1.79 M5 smart-bisect
add_icmg_test(test_atomize_cmd  tests/cli/test_atomize_cmd.cpp)   # v1.80 atomize T7
add_icmg_test(test_cron_tick    tests/core/test_cron_tick.cpp)    # v1.82 M6 cron-tick
add_icmg_test(test_cache_metrics       tests/core/test_cache_metrics.cpp)       # v1.84 M8 T9
add_icmg_test(test_backend_scorer      tests/embed/test_backend_scorer.cpp)     # v1.84 M8 T8
add_icmg_test(test_write_coalescer     tests/core/test_write_coalescer.cpp)     # v1.84 M8 T7
add_icmg_test(test_cache_break          tests/core/test_cache_break.cpp)         # v1.84 M8 T6
add_icmg_test(test_diminishing_returns tests/core/test_diminishing_returns.cpp) # v1.84 M8 T5
add_icmg_test(test_microcompact       tests/tkil/test_microcompact.cpp)        # v1.84 M8 T4
add_icmg_test(test_token_budget       tests/core/test_token_budget.cpp)        # v1.84 M8 T3
add_icmg_test(test_query_fingerprint  tests/core/test_query_fingerprint.cpp) # v1.84 M8 T2
add_icmg_test(test_curl_bin          tests/core/test_curl_bin.cpp)            # v1.87 M10 curl.exe fix
add_icmg_test(test_djb2_hash          tests/core/test_djb2_hash.cpp)           # v1.84 M8 T1
add_icmg_test(test_init_project_register  tests/cli/test_init_project_register.cpp)  # v1.83 M7-A
add_icmg_test(test_rules        tests/rules/test_rules.cpp)
add_icmg_test(test_data_store   tests/data/test_data_store.cpp)
add_icmg_test(test_global_db    tests/core/test_global_db.cpp)
add_icmg_test(test_abbr         tests/abbr/test_abbr.cpp)
add_icmg_test(test_sp           tests/sp/test_sp.cpp)
add_icmg_test(test_import       tests/import/test_import.cpp)
add_icmg_test(test_viz          tests/viz/test_viz.cpp)
add_icmg_test(test_mcp          tests/mcp/test_mcp.cpp)
add_icmg_test(test_code_search tests/mcp/test_code_search.cpp)  # v2.0.0 externals (code-search MCP)
add_icmg_test(test_mcp_toolset tests/mcp/test_mcp_toolset.cpp)  # v2.0.0 externals (dynamic toolsets)
add_icmg_test(test_graph_edges  tests/graph/test_graph_edges.cpp)
add_icmg_test(test_graph_report tests/graph/test_graph_report.cpp)        # v1.71 Graphify
add_icmg_test(test_defender_decision tests/cli/test_defender_decision.cpp) # v1.75 #187
add_icmg_test(test_sqlcipher_spike tests/core/test_sqlcipher_spike.cpp) # v1.76 T1
add_icmg_test(test_recall_cache tests/core/test_recall_cache.cpp) # ram-brain A
add_icmg_test(test_recall_cache_persist tests/core/test_recall_cache_persist.cpp) # v1.78.2 ram-brain persist A
add_icmg_test(test_recall_cache_writethrough tests/core/test_recall_cache_writethrough.cpp) # v1.78.2 ram-brain persist B
add_icmg_test(test_recall_cache_warmreload tests/core/test_recall_cache_warmreload.cpp) # v1.78.2 ram-brain persist C
add_icmg_test(test_recall_cache_persist_marker tests/core/test_recall_cache_persist_marker.cpp) # v1.78.2 ram-brain persist D
add_icmg_test(test_rcache_persist_wire tests/daemon/test_rcache_persist_wire.cpp) # v1.78.3 daemon wire
add_icmg_test(test_recall_cache_wiring tests/imem/test_recall_cache_wiring.cpp) # ram-brain B
add_icmg_test(test_rcache_rpc tests/daemon/test_rcache_rpc.cpp) # ram-brain C
add_icmg_test(test_governor_tick tests/core/test_governor_tick.cpp) # ram-brain D
add_icmg_test(test_db_key tests/core/test_db_key.cpp) # v1.76 T2-3
add_icmg_test(test_encrypt_backcompat tests/core/test_encrypt_backcompat.cpp) # v1.76 T4
add_icmg_test(test_encrypt_migration tests/core/test_encrypt_migration.cpp) # v1.76 T5+T7
add_icmg_test(test_precompact_output tests/core/test_precompact_output.cpp) # v1.75.1 precompact schema
add_icmg_test(test_zone_resolver tests/core/test_zone_resolver.cpp)
add_icmg_test(test_csharp_symbols tests/graph/test_csharp_symbols.cpp)
add_icmg_test(test_parallel tests/core/test_parallel.cpp)
add_icmg_test(test_embedder tests/embed/test_embedder.cpp)
add_icmg_test(test_graph_dedupe tests/graph/test_graph_dedupe.cpp)
add_icmg_test(test_graph_cache  tests/graph/test_graph_cache.cpp)  # v1.23.0 (TDD: S1)
add_icmg_test(test_find_basename tests/graph/test_find_basename.cpp)  # v1.29.0 (TDD: #10 ambiguity)
add_icmg_test(test_graph_prune  tests/graph/test_graph_prune.cpp)   # v2.0.0 Phase 0b (graph prune)
add_icmg_test(test_media_node   tests/graph/test_media_node.cpp)    # v2.0.0 Phase 3 (multimodal node)
add_icmg_test(test_agent_lease  tests/core/test_agent_lease.cpp)    # v2.0.0 Phase 4 (multi-agent lease)
add_icmg_test(test_db_key_hex   tests/core/test_db_key_hex.cpp)     # v1.90.0 sec (CodeQL sql-inj: hex key guard)
add_icmg_test(test_summary_confidence tests/core/test_summary_confidence.cpp)  # v2.0.0 TE1
add_icmg_test(test_compress_select tests/core/test_compress_select.cpp)  # v2.0.0 TE2
add_icmg_test(test_working_set tests/core/test_working_set.cpp)  # v2.0.0 C1+C3 governor core
add_icmg_test(test_ws_snapshot tests/core/test_ws_snapshot.cpp)  # v2.0.0 C4 transition
add_icmg_test(test_compact_advisor tests/core/test_compact_advisor.cpp)  # v2.0.0 C5 advisor
add_icmg_test(test_profile_key tests/core/test_profile_key.cpp)  # profile-store pure
add_icmg_test(test_profile_store tests/core/test_profile_store.cpp)  # profile-store CRUD
add_icmg_test(test_persona_init tests/core/test_persona_init.cpp)  # persona-continuity scaffold (identity-agnostic)
add_icmg_test(test_profile_source tests/core/test_profile_source.cpp)  # provenance: persona source column
add_icmg_test(test_prompt_history tests/core/test_prompt_history.cpp)  # prompt->response recall
add_icmg_test(test_prompt_suggest tests/core/test_prompt_suggest.cpp)  # v2.0.4 active reuse: Jaccard scorer + gated suggest
add_icmg_test(test_prompt_cluster tests/core/test_prompt_cluster.cpp)  # v2 pillar4: frequent-prompt clustering
add_icmg_test(test_command_suggest tests/core/test_command_suggest.cpp)  # v2.0.4 A: command recommender
add_icmg_test(test_feature_map tests/core/test_feature_map.cpp)  # feature-map M1: neighborsOf derived neighbors
add_icmg_test(test_crash_hint tests/core/test_crash_hint.cpp)  # err126 module-load self-diagnosing hint
add_icmg_test(test_flow_registry tests/core/test_flow_registry.cpp)  # v2.0.4 B: composite flows
add_icmg_test(test_transcript_extract tests/core/test_transcript_extract.cpp)  # v2.0.4 auto-record prompt-history
add_icmg_test(test_mode tests/core/test_mode.cpp)  # v2.0.4 mode banner persona-DB contract
add_icmg_test(test_cross_turn_dedup tests/core/test_cross_turn_dedup.cpp)  # v2.0.0 C2 dedup
add_icmg_test(test_structural_trim tests/core/test_structural_trim.cpp)  # v2.0.0 C6 tool-trim
add_icmg_test(test_trace_parse tests/core/test_trace_parse.cpp)  # v2.0.0 TE3
add_icmg_test(test_repo_skeleton tests/graph/test_repo_skeleton.cpp)  # v2.0.0 externals (repo-compact)
add_icmg_test(test_temporal     tests/graph/test_temporal.cpp)      # v2.0.0 externals (temporal KG)
add_icmg_test(test_api_spec     tests/core/test_api_spec.cpp)       # v2.0.0 externals (API spec)
add_icmg_test(test_fts_query     tests/core/test_fts_query.cpp)      # v2.0.0 search snapshot (FTS)
add_icmg_test(test_memory_tier tests/core/test_memory_tier.cpp)  # v2.0.0 externals (tiered memory)
add_icmg_test(test_bfs_expand  tests/graph/test_bfs_expand.cpp)
add_icmg_test(test_mcp_resources tests/mcp/test_mcp_resources.cpp)
add_icmg_test(test_lazy_tool_schema tests/mcp/test_lazy_tool_schema.cpp)
add_icmg_test(test_vec_search tests/embed/test_vec_search.cpp)
add_icmg_test(test_sp_link tests/sp/test_sp_link.cpp)
add_icmg_test(test_parity tests/graph/test_parity.cpp)
add_icmg_test(test_template tests/graph/test_template.cpp)
add_icmg_test(test_community tests/graph/test_community.cpp)
add_icmg_test(test_sql_symbols tests/graph/test_sql_symbols.cpp)
add_icmg_test(test_lint_style tests/cli/test_lint_style.cpp)
add_icmg_test(test_known_issue_auto tests/cli/test_known_issue_auto.cpp)
add_icmg_test(test_config     tests/cli/test_config.cpp)
add_icmg_test(test_init_hook  tests/cli/test_init_hook.cpp)
add_icmg_test(test_presence_hook  tests/cli/test_presence_hook.cpp)
add_icmg_test(test_zone_config tests/cli/test_zone_config.cpp)
add_icmg_test(test_review tests/cli/test_review.cpp)
add_icmg_test(test_audit tests/cli/test_audit.cpp)
add_icmg_test(test_drive_dialog_match tests/cli/test_drive_dialog_match.cpp)  # popup-killer B:/ predicate (heuristic 3)
add_icmg_test(test_agent_persona_policy tests/cli/test_agent_persona_policy.cpp)  # agent sub-agent persona policy
add_icmg_test(test_index tests/cli/test_index.cpp)
add_icmg_test(test_backend_factory tests/embed/test_backend_factory.cpp)
add_icmg_test(test_wordpiece tests/embed/test_wordpiece.cpp)
add_icmg_test(test_treesitter_c tests/graph/test_treesitter_c.cpp)
add_icmg_test(test_compressor tests/compress/test_compressor.cpp)
add_icmg_test(test_write_expander tests/compress/test_write_expander.cpp)  # v1.27.0 Phase 1.1
add_icmg_test(test_port_artifact tests/cli/test_port_artifact.cpp)         # v1.27.0 Phase 1.2
add_icmg_test(test_template_engine tests/compress/test_template_engine.cpp) # v1.27.0 Phase 2
add_icmg_test(test_treesitter_grammars tests/style_clone/test_treesitter_grammars.cpp) # v1.27.0 Phase 3
add_icmg_test(test_command_registry tests/cli/test_command_registry.cpp)   # v1.27.0 Phase 5
add_icmg_test(test_service_install_vbs tests/core/test_service_install_vbs.cpp)  # v1.28.0 TDD backfill
add_icmg_test(test_cache_emitter tests/cli/test_cache_emitter.cpp)
add_icmg_test(test_treesitter_typescript tests/graph/test_treesitter_typescript.cpp)
add_icmg_test(test_treesitter_python tests/graph/test_treesitter_python.cpp)
add_icmg_test(test_think_directive tests/cli/test_think_directive.cpp)
add_icmg_test(test_tool_call_cache tests/core/test_tool_call_cache.cpp)
add_icmg_test(test_file_lock tests/core/test_file_lock.cpp)
add_icmg_test(test_url_sanitize tests/core/test_url_sanitize.cpp)
add_icmg_test(test_fetch_reduce tests/core/test_fetch_reduce.cpp)
add_icmg_test(test_batch_emit tests/cli/test_batch_emit.cpp)
add_icmg_test(test_filter_pass tests/tkil/test_filter_pass.cpp)
add_icmg_test(test_ref_registry tests/cli/test_ref_registry.cpp)
add_icmg_test(test_pack_delta      tests/cli/test_pack_delta.cpp)
add_icmg_test(test_update_asset    tests/cli/test_update_asset.cpp)
add_icmg_test(test_sync_roundtrip  tests/cli/test_sync_roundtrip.cpp)
add_icmg_test(test_daemon          tests/cli/test_daemon.cpp)
add_icmg_test(test_rule_daemon     tests/daemon/test_rule_daemon.cpp)
add_icmg_test(test_hook_internals  tests/core/test_hook_internals.cpp)
add_icmg_test(test_hook_cmd        tests/cli/test_hook_cmd.cpp)
add_icmg_test(test_bench_cmd       tests/cli/test_bench_cmd.cpp)
add_icmg_test(test_skill_index_cmd tests/cli/test_skill_index_cmd.cpp)
add_icmg_test(test_hook_enforce    tests/core/test_hook_enforce.cpp)
add_icmg_test(test_service_cmd     tests/cli/test_service_cmd.cpp)
add_icmg_test(test_service_install_helper tests/core/test_service_install_helper.cpp)
add_icmg_test(test_skill_manifest  tests/cli/test_skill_manifest.cpp)
add_icmg_test(test_copy_cmd        tests/cli/test_copy_cmd.cpp)
add_icmg_test(test_tokens_cmd      tests/cli/test_tokens_cmd.cpp)
add_icmg_test(test_ingest_history_cmd tests/cli/test_ingest_history_cmd.cpp)
add_icmg_test(test_migration_0029     tests/core/test_migration_0029.cpp)
add_icmg_test(test_migration_0030     tests/core/test_migration_0030.cpp)
add_icmg_test(test_rules_store        tests/core/test_rules_store.cpp)
add_icmg_test(test_rules_cmd          tests/cli/test_rules_cmd.cpp)
add_icmg_test(test_skill_ask_cmd      tests/cli/test_skill_ask_cmd.cpp)
add_icmg_test(test_hook_skill_suggest tests/core/test_hook_skill_suggest.cpp)

# v1.26.0: previously 3 standalone test exes (test_claudemd / test_backup /
# test_diff_summary) bypassed the macro because they didn't need icmg_lib.
# Mono icmg_test target absorbs them â€” their .obj is tiny vs the link saving
# of dropping 3 separate executables.
add_icmg_test(test_claudemd      tests/cli/test_claudemd.cpp)
add_icmg_test(test_backup        tests/cli/test_backup.cpp)
add_icmg_test(test_diff_summary  tests/cli/test_diff_summary.cpp)
add_icmg_test(test_install_cmd     tests/cli/test_install_cmd.cpp)
add_icmg_test(test_session_state   tests/core/test_session_state.cpp)
add_icmg_test(test_stream_filter   tests/tkil/test_stream_filter.cpp)
add_icmg_test(test_migration_0028  tests/core/test_migration_0028.cpp)
add_icmg_test(test_symbol_resolver tests/core/test_symbol_resolver.cpp)
add_icmg_test(test_skill_chunker      tests/imem/test_skill_chunker.cpp)
add_icmg_test(test_skill_chunk_embed  tests/imem/test_skill_chunk_embed.cpp)
add_icmg_test(test_bug_report_cmd  tests/cli/test_bug_report_cmd.cpp)
add_icmg_test(test_focus_chain     tests/imem/test_focus_chain.cpp)
add_icmg_test(test_focus_cmd       tests/cli/test_focus_cmd.cpp)
add_icmg_test(test_skill_chunk_cmd tests/cli/test_skill_chunk_cmd.cpp)
add_icmg_test(test_ast_compressor   tests/graph/test_ast_compressor.cpp)
add_icmg_test(test_hook_test_fail_context tests/core/test_hook_test_fail_context.cpp)

add_icmg_test(test_secret_scanner     tests/core/test_secret_scanner.cpp)
add_icmg_test(test_target_disambiguator tests/core/test_target_disambiguator.cpp)
add_icmg_test(test_target_verify_cmd    tests/cli/test_target_verify_cmd.cpp)
add_icmg_test(test_safe_rollback_cmd    tests/cli/test_safe_rollback_cmd.cpp)
add_icmg_test(test_hook_post_edit_sync  tests/core/test_hook_post_edit_sync.cpp)
add_icmg_test(test_migration_0031     tests/core/test_migration_0031.cpp)
add_icmg_test(test_migration_0032       tests/core/test_migration_0032.cpp)
add_icmg_test(test_approach_cmd         tests/cli/test_approach_cmd.cpp)
add_icmg_test(test_hook_approach        tests/core/test_hook_approach.cpp)
add_icmg_test(test_bundle_secret_scan   tests/cli/test_bundle_secret_scan.cpp)
# v1.10.0 backlog burn:
add_icmg_test(test_hookio_cmd           tests/cli/test_hookio_cmd.cpp)
add_icmg_test(test_cleanup_cmd          tests/cli/test_cleanup_cmd.cpp)
add_icmg_test(test_cron_store           tests/core/test_cron_store.cpp)
add_icmg_test(test_context_budget_users tests/cli/test_context_budget_users.cpp)
add_icmg_test(test_savings_user_panel   tests/cli/test_savings_user_panel.cpp)
# v1.11.0 backlog:
add_icmg_test(test_path_clean_cmd       tests/cli/test_path_clean_cmd.cpp)
add_icmg_test(test_sweep_legacy_schtasks tests/core/test_sweep_legacy_schtasks.cpp)
# v1.17.0 backlog burn:
add_icmg_test(test_turn_cache           tests/core/test_turn_cache.cpp)
add_icmg_test(test_inject_dedup         tests/core/test_inject_dedup.cpp)
add_icmg_test(test_session_inject       tests/cli/test_session_inject.cpp)
add_icmg_test(test_session_greeting      tests/cli/test_session_greeting.cpp)
add_icmg_test(test_agent_task            tests/cli/test_agent_task.cpp)
add_icmg_test(test_self_info_detect      tests/cli/test_self_info_detect.cpp)
add_icmg_test(test_agent_model           tests/cli/test_agent_model.cpp)
add_icmg_test(test_agent_complexity      tests/cli/test_agent_complexity.cpp)
add_icmg_test(test_persona_surface       tests/cli/test_persona_surface.cpp)
add_icmg_test(test_chat_template        tests/llm/test_chat_template.cpp)
add_icmg_test(test_auto_rule           tests/cli/test_auto_rule.cpp)         # v1.51.0 TDD
add_icmg_test(test_skill_crud          tests/cli/test_skill_crud.cpp)        # v1.51.0 TDD
add_icmg_test(test_warm_pipe           tests/llm/test_warm_pipe.cpp)          # v1.52.0 TDD
add_icmg_test(test_warm_loop_integration tests/llm/test_warm_loop_integration.cpp) # v1.52.0 TDD
add_icmg_test(test_router                tests/cli/test_router.cpp)               # v1.53.0 TDD
add_icmg_test(test_confidence            tests/cli/test_confidence.cpp)           # v1.61 F10
add_icmg_test(test_sayless_resolve       tests/cli/test_sayless_resolve.cpp)      # v1.78.1 (renamed from test_caveman_resolve v1.66)
add_icmg_test(test_ritual_gate            tests/cli/test_ritual_gate.cpp)         # ritual gate (post-change 5-sync enforcement) 2026-06-11
add_icmg_test(test_discipline_score       tests/cli/test_discipline_score.cpp)    # feature-coverage scorecard 2026-06-11
add_icmg_test(test_recall_gate            tests/cli/test_recall_gate.cpp)         # pre-task recall gate 2026-06-11
add_icmg_test(test_sayless_migration     tests/cli/test_sayless_migration.cpp)    # v1.78.1
add_icmg_test(test_gist                  tests/cli/test_gist.cpp)                 # v1.63 F12
add_icmg_test(test_mine                  tests/cli/test_mine.cpp)                 # v1.63 F8
add_icmg_test(test_bugfix_v161           tests/core/test_bugfix_v161.cpp)         # v1.61 bugfix
add_icmg_test(test_exec_timeout          tests/core/test_exec_timeout.cpp)        # v1.78.4 bug2: safeExecShell pipe-block
add_icmg_test(test_updating_lock         tests/core/test_updating_lock.cpp)       # v1.78.4 bug1: updating.lock startup check

# ---------------------------------------------------------------------------
# v1.29.0 #2: opt-in mono icmg_test binary. Collapses 122 link steps into 1.
# Build saving target: ~40-50% of test-link phase (each per-exe is ~17MB
# whole-archive link from libicmg_lib.a â€” mono only links once).
#
# Each tests/**/test_*.cpp has its `int main()` guarded by ICMG_MONO_TEST.
# When this option is ON, the guards collapse, mono_main.cpp provides the
# single `main()` + betweenTests() impl that resets Scorer singleton
# between TESTs (prevents BM25 corpus stat leak that caused the v1.26.0
# attempt to hang).
#
# Per-exe model REMAINS the default (this option is OFF unless flipped).
# When ON, per-exe targets still build alongside â€” both work.
# ---------------------------------------------------------------------------
# (ICMG_MONO_TEST options moved earlier â€” see before macro add_icmg_test)
if(ICMG_MONO_TEST)
    get_property(_ICMG_TEST_SRCS GLOBAL PROPERTY ICMG_ALL_TEST_SOURCES)
    message(STATUS "icmg: ICMG_MONO_TEST=ON â€” building mono icmg_test (${_ICMG_TEST_SRCS} sources collected)")
    add_executable(icmg_test ${_ICMG_TEST_SRCS} tests/mono_main.cpp)
    target_compile_definitions(icmg_test PRIVATE
        ICMG_MONO_TEST=1
        ICMG_MONO_TEST_RESET=1)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(APPLE)
            target_link_libraries(icmg_test PRIVATE
                -Wl,-force_load,$<TARGET_FILE:icmg_lib> icmg_lib)
        else()
            target_link_libraries(icmg_test PRIVATE
                -Wl,--whole-archive icmg_lib -Wl,--no-whole-archive)
        endif()
        if(WIN32)
            target_link_libraries(icmg_test PRIVATE
                -static-libgcc -static-libstdc++
                -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
        endif()
    else()
        # v1.54.0 Sub-C fix: MSVC /WHOLEARCHIVE pulls static-init
        # ICMG_REGISTER_* macros from icmg_lib.lib (mirrors main icmg exe link).
        # Without it, MCP tools + cmds dropped from mono test -> "Registry: unknown key".
        target_link_libraries(icmg_test PRIVATE icmg_lib)
        if(MSVC)
            target_link_options(icmg_test PRIVATE "/WHOLEARCHIVE:icmg_lib")
        endif()
    endif()
    target_include_directories(icmg_test PRIVATE tests src third_party)
    add_test(NAME icmg_test COMMAND icmg_test)
endif()

# ---------------------------------------------------------------------------
# v1.31.0 A1: llama.cpp vendored under third_party/llama.cpp/ (tag b9297).
# Opt-in via -DICMG_USE_LLAMA=ON. Default OFF â€” preserves baseline icmg.exe
# size (~25MB) and zero impact when feature off.
#
# Backends:
#   - CPU (GGML_CPU=ON, always when llama enabled)
#   - Vulkan opt-in: -DICMG_LLAMA_VULKAN=ON (forwards to GGML_VULKAN)
#   - Other GPU backends (CUDA/SYCL/OpenCL/Metal/etc.) NOT vendored â€”
#     gitignored to keep repo size sane. Custom builds may sideload.
#
# When ON: ICMG_HAS_LLAMA=1 compile def propagates to icmg_lib.
# When OFF: zero llama.cpp code in icmg_lib; baseline performance.
# ---------------------------------------------------------------------------
option(ICMG_USE_LLAMA "Build with local LLM via vendored llama.cpp (opt-in)" OFF)
# v1.32.0 B1 reverted: default ON attempt hit MSYS2/MinGW compiler crash
# (0xc0000139) on llama.cpp ggml-cpu/amx/mmq.cpp + binary-ops.cpp under
# -march=native. Likely AMX intrinsic header incompat. Opt-in build is
# verified working (link succeeds); default OFF preserves baseline shippable
# Windows binary. Revisit when MSYS2 toolchain or llama.cpp GGML AMX path
# stabilizes (b9298+ may fix).
option(ICMG_LLAMA_VULKAN "Enable Vulkan backend for llama.cpp (requires ICMG_USE_LLAMA=ON)" OFF)
if(ICMG_USE_LLAMA)
    message(STATUS "icmg: ICMG_USE_LLAMA=ON â€” vendoring llama.cpp from third_party/llama.cpp/")
    # v1.40.0 B1-toolchain-fix: force-disable GGML_NATIVE (-march=native AVX
    # intrinsics) + GGML_CCACHE wrapper. Both trigger c++.exe crash
    # (0xc0000139 STATUS_ENTRYPOINT_NOT_FOUND) on MSYS2 GCC 15.2 when
    # compiling ggml-cpu/amx/mmq.cpp + binary-ops.cpp. With these OFF,
    # the opt-in build succeeds. Users on other toolchains may override
    # with -DGGML_NATIVE=ON -DGGML_CCACHE=ON at configure time.
    set(GGML_NATIVE           OFF CACHE BOOL "" FORCE)
    set(GGML_CCACHE           OFF CACHE BOOL "" FORCE)
    # v1.40.0: GGML_NATIVE=OFF for MSYS2 toolchain safety, but explicitly enable
    # Haswell+ ISA features (AVX2/FMA/F16C/BMI2) to recover ~10x perf vs baseline.
    # Skip AVX512+AMX (those trigger the original AMX-intrinsic compiler crash).
    set(GGML_SSE42            ON  CACHE BOOL "" FORCE)
    set(GGML_AVX              ON  CACHE BOOL "" FORCE)
    set(GGML_AVX2             ON  CACHE BOOL "" FORCE)
    set(GGML_FMA              ON  CACHE BOOL "" FORCE)
    set(GGML_F16C             ON  CACHE BOOL "" FORCE)
    set(GGML_BMI2             ON  CACHE BOOL "" FORCE)
    # Force-disable upstream build options we don't ship.
    set(LLAMA_STANDALONE      OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_HTML      OFF CACHE BOOL "" FORCE)
    # LLAMA_BUILD_COMMON triggers vendor/cpp-httplib subdir which we didn't
    # vendor (server REST infra unneeded â€” icmg uses libllama directly).
    # OFF for now; LlamaRunner replicates the few helpers we need.
    set(LLAMA_BUILD_COMMON    OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS     OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER    OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_UI        OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_APP       OFF CACHE BOOL "" FORCE)
    set(LLAMA_CURL            OFF CACHE BOOL "" FORCE)
    # GGML backend selection â€” CPU always; Vulkan per ICMG_LLAMA_VULKAN.
    set(GGML_CPU              ON  CACHE BOOL "" FORCE)
    set(GGML_VULKAN           ${ICMG_LLAMA_VULKAN} CACHE BOOL "" FORCE)
    # Explicitly disable GPU backends we did NOT vendor.
    set(GGML_CUDA             OFF CACHE BOOL "" FORCE)
    set(GGML_HIP              OFF CACHE BOOL "" FORCE)
    set(GGML_METAL            OFF CACHE BOOL "" FORCE)
    set(GGML_SYCL             OFF CACHE BOOL "" FORCE)
    set(GGML_OPENCL           OFF CACHE BOOL "" FORCE)
    set(GGML_BLAS             OFF CACHE BOOL "" FORCE)
    set(GGML_RPC              OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/llama.cpp EXCLUDE_FROM_ALL)
    # v1.42.0 MSVC compat: u8"" ? char8_t under C++20+. /Zc:char8_t- restores legacy.
    if(MSVC AND TARGET llama)
        target_compile_options(llama PRIVATE /Zc:char8_t-)
    endif()
    if(MSVC AND TARGET ggml)
        target_compile_options(ggml PRIVATE /Zc:char8_t-)
    endif()
    # v1.47.0: eliminate shared-PDB race on ggml-vulkan target.
    # 250+ shader .cpp files write debug info to ONE PDB by default
    # under /MP parallel cl.exe â†’ "Cannot open compiler generated
    # file: Invalid argument" intermittent fail. /Z7 embeds debug
    # info in the .obj itself, no shared PDB â†’ zero race.
    if(MSVC AND TARGET ggml-vulkan)
        target_compile_options(ggml-vulkan PRIVATE /Z7)
    endif()
    target_compile_definitions(icmg_lib PUBLIC ICMG_HAS_LLAMA=1)
    target_include_directories(icmg_lib PUBLIC
        third_party/llama.cpp/include
        third_party/llama.cpp/common)
    target_link_libraries(icmg_lib PUBLIC llama)
    message(STATUS "icmg: llama.cpp ENABLED (CPU${_vulkan_state})")
endif()
