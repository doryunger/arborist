cmake_minimum_required(VERSION 3.20)
project(bt_framework VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(yaml-cpp REQUIRED)

# ── Library ───────────────────────────────────────────────────────────────────
file(GLOB_RECURSE BT_SOURCES
    src/runtime/*.cpp
    src/builder/*.cpp
    src/validation/*.cpp
    src/schema/*.cpp
)

if(BT_SOURCES)
    add_library(bt_framework ${BT_SOURCES})
    target_include_directories(bt_framework PUBLIC include)
    target_link_libraries(bt_framework PUBLIC yaml-cpp)
else()
    add_library(bt_framework INTERFACE)
    target_include_directories(bt_framework INTERFACE include)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
