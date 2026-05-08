cmake_minimum_required(VERSION 3.20)
project(icmg VERSION 0.18.0 LANGUAGES CXX C)

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
    find_library(ORT_LIB NAMES onnxruntime
                 PATHS /usr/local/lib /opt/homebrew/lib /usr/lib
                 ENV   ORT_LIB_DIR)
    if(ORT_LIB)
        message(STATUS "icmg: ONNX backend ENABLED (lib: ${ORT_LIB})")
        target_compile_definitions(icmg_lib PUBLIC ICMG_HAS_ONNX=1)
        target_link_libraries(icmg_lib PUBLIC ${ORT_LIB})
    else()
        message(WARNING "icmg: ICMG_USE_ONNX=ON but onnxruntime not found; "
                "install ORT (https://onnxruntime.ai) then retry. "
                "Build proceeds without ONNX backend.")
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
add_executable(icmg src/main.cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_libraries(icmg PRIVATE
        -Wl,--whole-archive icmg_lib -Wl,--no-whole-archive)
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
