cmake_minimum_required(VERSION 3.20)
project(icmg VERSION 0.32.10 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Third-party: SQLite3 amalgamation
# ---------------------------------------------------------------------------
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
)

# Third-party: nlohmann/json single header
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party)

# ---------------------------------------------------------------------------
# icmg_lib — all src/ except main.cpp (shared by binary + tests)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE ALL_SOURCES "src/*.cpp")
list(FILTER ALL_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_library(icmg_lib STATIC ${ALL_SOURCES})
target_include_directories(icmg_lib PUBLIC src third_party)
target_link_libraries(icmg_lib PUBLIC sqlite3 nlohmann_json)

if(WIN32)
    target_link_libraries(icmg_lib PUBLIC ws2_32 shlwapi)
    target_compile_definitions(icmg_lib PUBLIC
        _WIN32_WINNT=0x0601 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(UNIX)
    target_link_libraries(icmg_lib PUBLIC pthread dl)
endif()

# ---------------------------------------------------------------------------
# Phase 33 — optional backends. Both default OFF; system-detected on opt-in.
# ---------------------------------------------------------------------------
option(ICMG_USE_ONNX        "Build ONNX Runtime embedder backend (Phase 33+)" OFF)
option(ICMG_USE_TREESITTER  "Build tree-sitter AST symbol extractor (Phase 33+)" OFF)

if(ICMG_USE_ONNX)
    set(ORT_INCLUDE ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/include)
    set(ORT_DLL     ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/lib/onnxruntime.dll)
    set(ORT_VERSION "1.25.1")
    set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-win-x64-${ORT_VERSION}.zip")

    # Auto-download DLL on Windows if missing (header is already vendored).
    if(WIN32 AND NOT EXISTS ${ORT_DLL})
        message(STATUS "icmg: fetching onnxruntime ${ORT_VERSION} (one-time, ~75MB)...")
        file(DOWNLOAD ${ORT_URL} ${CMAKE_BINARY_DIR}/_ort.zip
             SHOW_PROGRESS STATUS _ort_status)
        list(GET _ort_status 0 _ort_rc)
        if(_ort_rc EQUAL 0)
            file(ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/_ort.zip
                 DESTINATION ${CMAKE_BINARY_DIR}/_ort_unpack)
            file(GLOB _ort_dlls
                 "${CMAKE_BINARY_DIR}/_ort_unpack/onnxruntime-win-x64-${ORT_VERSION}/lib/*.dll")
            file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/lib)
            foreach(_d ${_ort_dlls})
                file(COPY ${_d} DESTINATION ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/lib)
            endforeach()
            file(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/_ort_unpack ${CMAKE_BINARY_DIR}/_ort.zip)
        else()
            message(WARNING "icmg: ORT auto-download failed (${_ort_status}); "
                    "fetch manually per third_party/onnxruntime/README.md")
        endif()
    endif()

    if(EXISTS ${ORT_DLL} AND EXISTS ${ORT_INCLUDE}/onnxruntime_c_api.h)
        message(STATUS "icmg: ONNX backend ENABLED (DLL: ${ORT_DLL})")
        target_compile_definitions(icmg_lib PUBLIC ICMG_HAS_ONNX=1)
        target_include_directories(icmg_lib PUBLIC ${ORT_INCLUDE})
        # MinGW gcc can link directly against a Microsoft-built DLL when the
        # exported symbols use C linkage (which the ORT C API does).
        target_link_libraries(icmg_lib PUBLIC ${ORT_DLL})
        # Bundle DLL next to executable.
        if(WIN32)
            file(COPY ${ORT_DLL} DESTINATION ${CMAKE_BINARY_DIR})
            set(ORT_PROVIDERS_DLL ${CMAKE_SOURCE_DIR}/third_party/onnxruntime/lib/onnxruntime_providers_shared.dll)
            if(EXISTS ${ORT_PROVIDERS_DLL})
                file(COPY ${ORT_PROVIDERS_DLL} DESTINATION ${CMAKE_BINARY_DIR})
            endif()
        endif()
    else()
        message(WARNING "icmg: ICMG_USE_ONNX=ON but ORT DLL/header not found "
                "(DLL=${ORT_DLL}, header=${ORT_INCLUDE}). "
                "Build proceeds without ONNX backend. "
                "See third_party/onnxruntime/README.md for manual install.")
    endif()
endif()

if(ICMG_USE_TREESITTER)
    find_path(TS_INCLUDE tree_sitter/api.h
              PATHS /usr/local/include /opt/homebrew/include /usr/include
                    /c/msys64/mingw64/include /mingw64/include)
    find_library(TS_LIB NAMES tree-sitter
                 PATHS /usr/local/lib /opt/homebrew/lib /usr/lib
                       /c/msys64/mingw64/lib /mingw64/lib)
    set(TS_C_GRAMMAR ${CMAKE_SOURCE_DIR}/third_party/tree-sitter-c/src/parser.c)
    if(TS_INCLUDE AND TS_LIB AND EXISTS ${TS_C_GRAMMAR})
        message(STATUS "icmg: tree-sitter ENABLED (lib: ${TS_LIB}, C grammar vendored)")
        # Vendored grammar — pure C, no extra deps.
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

        # Copy runtime DLLs next to build outputs (Windows). Resolves
        # 0xc0000135 / "libX.dll not found" at exe launch.
        # libtree-sitter transitively pulls wasmtime + zstd on MSYS2.
        if(WIN32)
            set(TS_DLL_NAMES
                libtree-sitter-0.26.dll libtree-sitter.dll
                wasmtime.dll libzstd.dll
                libwinpthread-1.dll)
            foreach(_dll IN LISTS TS_DLL_NAMES)
                find_file(TS_DLL_${_dll} NAMES ${_dll}
                          PATHS /c/msys64/mingw64/bin /mingw64/bin
                                ${TS_INCLUDE}/../bin)
                if(TS_DLL_${_dll})
                    message(STATUS "icmg: copy DLL ${_dll}")
                    file(COPY ${TS_DLL_${_dll}} DESTINATION ${CMAKE_BINARY_DIR})
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
    target_link_libraries(icmg PRIVATE
        -Wl,--whole-archive icmg_lib -Wl,--no-whole-archive)
    # Static link runtime → portable exe (no mingw DLL deps).
    if(WIN32)
        target_link_options(icmg PRIVATE
            -static-libgcc -static-libstdc++
            -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
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
        target_link_libraries(${name} PRIVATE
            -Wl,--whole-archive icmg_lib -Wl,--no-whole-archive)
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
add_icmg_test(test_mcp_resources tests/mcp/test_mcp_resources.cpp)
add_icmg_test(test_vec_search tests/embed/test_vec_search.cpp)
add_icmg_test(test_sp_link tests/sp/test_sp_link.cpp)
add_icmg_test(test_parity tests/graph/test_parity.cpp)
add_icmg_test(test_template tests/graph/test_template.cpp)
add_icmg_test(test_community tests/graph/test_community.cpp)
add_icmg_test(test_sql_symbols tests/graph/test_sql_symbols.cpp)
add_icmg_test(test_lint_style tests/cli/test_lint_style.cpp)
add_icmg_test(test_known_issue_auto tests/cli/test_known_issue_auto.cpp)
add_icmg_test(test_config tests/cli/test_config.cpp)
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
