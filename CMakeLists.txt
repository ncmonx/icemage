cmake_minimum_required(VERSION 3.20)
project(icmg VERSION 0.1.0 LANGUAGES CXX C)

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

# Warnings on icmg_lib
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(icmg_lib PRIVATE -Wall -Wextra -Wno-unused-parameter)
elseif(MSVC)
    target_compile_options(icmg_lib PRIVATE /W4 /wd4100)
endif()

# ---------------------------------------------------------------------------
# icmg executable
# ---------------------------------------------------------------------------
add_executable(icmg src/main.cpp)
target_link_libraries(icmg PRIVATE icmg_lib)

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
macro(add_icmg_test name source_file)
    add_executable(${name} ${source_file})
    target_link_libraries(${name} PRIVATE icmg_lib)
    target_include_directories(${name} PRIVATE tests src third_party)
    add_test(NAME ${name} COMMAND ${name})
endmacro()

add_icmg_test(test_parseargv    tests/rtk/test_parseargv.cpp)
add_icmg_test(test_detector     tests/rtk/test_detector.cpp)
add_icmg_test(test_filter_utils tests/rtk/test_filter_utils.cpp)
add_icmg_test(test_filters      tests/rtk/test_filters.cpp)
add_icmg_test(test_db           tests/core/test_db.cpp)
add_icmg_test(test_scorer       tests/icm/test_scorer.cpp)
add_icmg_test(test_rules        tests/rules/test_rules.cpp)
