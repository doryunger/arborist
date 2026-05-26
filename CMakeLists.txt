cmake_minimum_required(VERSION 3.20)
project(bt_framework VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_CLANG_TIDY       "Run clang-tidy during build"  ON)
option(ARBORIST_BUILD_TESTS     "Build test suite"             ON)
option(ARBORIST_BUILD_EXAMPLES  "Build examples"               ON)
option(ARBORIST_BUILD_BENCHMARKS "Build benchmarks"            ON)
option(ARBORIST_BUILD_TOOLS     "Build playground and editor"  ON)
option(ARBORIST_INSTALL         "Generate install rules"       OFF)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy not found — linting disabled")
    endif()
endif()

find_package(yaml-cpp REQUIRED)
find_package(SQLite3  REQUIRED)
find_package(Threads  REQUIRED)

find_package(httplib QUIET)
if(NOT httplib_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.18.1
    )
    set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(httplib)
endif()

file(GLOB_RECURSE BT_SOURCES
    src/runtime/*.cpp
    src/builder/*.cpp
    src/validation/*.cpp
    src/schema/*.cpp
    src/registry/*.cpp
    src/simulator/*.cpp
    src/monitor/*.cpp
    src/harness/*.cpp
    src/analysis/*.cpp
    src/adapter/*.cpp
)

if(BT_SOURCES)
    add_library(bt_framework ${BT_SOURCES})
    target_include_directories(bt_framework PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(bt_framework PUBLIC  yaml-cpp SQLite::SQLite3)
    target_link_libraries(bt_framework PRIVATE httplib::httplib Threads::Threads)
else()
    add_library(bt_framework INTERFACE)
    target_include_directories(bt_framework INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
endif()

if(ARBORIST_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS bt_framework
        EXPORT  arborist-poc-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT arborist-poc-targets
        FILE      arborist-poc-config.cmake
        NAMESPACE arborist_poc::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/arborist-poc
    )
endif()

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE ALL_CXX_FILES
        include/bt/*.h
        src/**/*.cpp
        src/**/*.h
        tests/*.cpp
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_CXX_FILES}
        COMMENT "Formatting source files"
    )
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${ALL_CXX_FILES}
        COMMENT "Checking source formatting"
    )
endif()

if(ARBORIST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(ARBORIST_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(ARBORIST_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
if(ARBORIST_BUILD_TOOLS)
    add_subdirectory(playground)
    add_subdirectory(editor)
endif()
