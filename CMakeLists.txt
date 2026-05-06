cmake_minimum_required(VERSION 3.20)
project(icmg VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# SQLite3 amalgamation (place sqlite3.h + sqlite3.c in third_party/sqlite3/)
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
)

# nlohmann/json single header
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party)

# Collect all sources
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(icmg ${SOURCES})
target_link_libraries(icmg PRIVATE sqlite3 nlohmann_json)
target_include_directories(icmg PRIVATE src third_party)

if(WIN32)
    target_link_libraries(icmg PRIVATE ws2_32 shlwapi)
    target_compile_definitions(icmg PRIVATE _WIN32_WINNT=0x0601 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(UNIX)
    target_link_libraries(icmg PRIVATE pthread dl)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(icmg PRIVATE -Wall -Wextra -Wno-unused-parameter)
elseif(MSVC)
    target_compile_options(icmg PRIVATE /W4 /wd4100)
endif()

# Strip release binary
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(UNIX AND NOT APPLE)
        target_link_options(icmg PRIVATE -s)
    endif()
endif()
