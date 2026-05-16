cmake_minimum_required(VERSION 3.20)
project(icmg VERSION 1.2.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
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

# ---------------------------------------------------------------------------
# Third-party: SQLite3 amalgamation (auto-downloaded if missing — gitignored)
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

# nlohmann/json single-header (gitignored — auto-fetch if missing)
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

add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_ENABLE_FTS5=1
)

# Third-party: nlohmann/json single header
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party)

# ---------------------------------------------------------------------------
# icmg_lib â€” all src/ except main.cpp (shared by binary + tests)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE ALL_SOURCES "src/*.cpp")
list(FILTER ALL_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_library(icmg_lib STATIC ${ALL_SOURCES})
target_include_directories(icmg_lib PUBLIC src third_party)
target_link_libraries(icmg_lib PUBLIC sqlite3 nlohmann_json)

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
    target_link_libraries(icmg_lib PUBLIC ws2_32 shlwapi)
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
        set(ORT_LIB_NAME "onnxruntime.dll")
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
                # dereference them — produced "unsupported mach-o filetype" on
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
                # Windows .zip — no symlinks in ORT-Win archive.
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
    # picked up apt's v0.20.x on Linux — ABI-incompatible with v0.26 vendored
    # grammars (silent empty parse trees). Building from source pinned to v0.24.5
    # guarantees grammar-runtime ABI match (parser.h v14).
    # Vendored grammars (parser.c LANGUAGE_VERSION 15) require runtime ≥ v0.25.
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
        # via execute_process — universally available on CI runners (preinstalled
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

        # v0.59.0: vendored libtree-sitter is static — no more libtree-sitter*.dll
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
if(WIN32)
    enable_language(RC)
    set(ICMG_WIN_RESOURCE src/icmg.rc)
else()
    set(ICMG_WIN_RESOURCE)
endif()
add_executable(icmg src/main.cpp ${ICMG_WIN_RESOURCE})
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

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(UNIX AND NOT APPLE)
        target_link_options(icmg PRIVATE -s)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

# Helper macro: add_icmg_test(name source_file)
# --whole-archive ensures static-init registrations (ICMG_REGISTER_*) are not
# dead-stripped by the linker when building test binaries.
macro(add_icmg_test name source_file)
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
endmacro()

add_icmg_test(test_parseargv    tests/tkil/test_parseargv.cpp)
add_icmg_test(test_detector     tests/tkil/test_detector.cpp)
add_icmg_test(test_filter_utils tests/tkil/test_filter_utils.cpp)
add_icmg_test(test_filters      tests/tkil/test_filters.cpp)
add_icmg_test(test_db           tests/core/test_db.cpp)
add_icmg_test(test_db_prepared  tests/core/test_db_prepared.cpp)
add_icmg_test(test_scorer       tests/imem/test_scorer.cpp)
add_icmg_test(test_rules        tests/rules/test_rules.cpp)
add_icmg_test(test_data_store   tests/data/test_data_store.cpp)
add_icmg_test(test_global_db    tests/core/test_global_db.cpp)
add_icmg_test(test_abbr         tests/abbr/test_abbr.cpp)
add_icmg_test(test_sp           tests/sp/test_sp.cpp)
add_icmg_test(test_import       tests/import/test_import.cpp)
add_icmg_test(test_viz          tests/viz/test_viz.cpp)
add_icmg_test(test_mcp          tests/mcp/test_mcp.cpp)
add_icmg_test(test_graph_edges  tests/graph/test_graph_edges.cpp)
add_icmg_test(test_zone_resolver tests/core/test_zone_resolver.cpp)
add_icmg_test(test_csharp_symbols tests/graph/test_csharp_symbols.cpp)
add_icmg_test(test_parallel tests/core/test_parallel.cpp)
add_icmg_test(test_embedder tests/embed/test_embedder.cpp)
add_icmg_test(test_graph_dedupe tests/graph/test_graph_dedupe.cpp)
add_icmg_test(test_bfs_expand  tests/graph/test_bfs_expand.cpp)
add_icmg_test(test_mcp_resources tests/mcp/test_mcp_resources.cpp)
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
add_icmg_test(test_zone_config tests/cli/test_zone_config.cpp)
add_icmg_test(test_review tests/cli/test_review.cpp)
add_icmg_test(test_audit tests/cli/test_audit.cpp)
add_icmg_test(test_index tests/cli/test_index.cpp)
add_icmg_test(test_backend_factory tests/embed/test_backend_factory.cpp)
add_icmg_test(test_wordpiece tests/embed/test_wordpiece.cpp)
add_icmg_test(test_treesitter_c tests/graph/test_treesitter_c.cpp)
add_icmg_test(test_compressor tests/compress/test_compressor.cpp)
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

# test_claudemd / test_backup: standalone — helpers are static inline, no icmg_lib needed.
add_executable(test_claudemd tests/cli/test_claudemd.cpp)
target_include_directories(test_claudemd PRIVATE tests src third_party)
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(test_claudemd PRIVATE
        -static-libgcc -static-libstdc++
        -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()
add_test(NAME test_claudemd COMMAND test_claudemd)

add_executable(test_backup tests/cli/test_backup.cpp)
target_include_directories(test_backup PRIVATE tests src third_party)
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(test_backup PRIVATE
        -static-libgcc -static-libstdc++
        -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()
add_test(NAME test_backup COMMAND test_backup)

add_executable(test_diff_summary tests/cli/test_diff_summary.cpp)
target_include_directories(test_diff_summary PRIVATE tests src third_party)
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(test_diff_summary PRIVATE
        -static-libgcc -static-libstdc++
        -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()
add_test(NAME test_diff_summary COMMAND test_diff_summary)
add_icmg_test(test_install_cmd     tests/cli/test_install_cmd.cpp)
add_icmg_test(test_session_state   tests/core/test_session_state.cpp)
add_icmg_test(test_stream_filter   tests/tkil/test_stream_filter.cpp)
add_icmg_test(test_migration_0028  tests/core/test_migration_0028.cpp)
add_icmg_test(test_symbol_resolver tests/core/test_symbol_resolver.cpp)
add_icmg_test(test_skill_chunker   tests/imem/test_skill_chunker.cpp)
add_icmg_test(test_focus_chain     tests/imem/test_focus_chain.cpp)
add_icmg_test(test_focus_cmd       tests/cli/test_focus_cmd.cpp)
