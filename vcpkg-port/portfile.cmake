vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO doryunger/arborist
    REF 68a4449b9f61a216ce191a00aa3e7036db79e2dd
    SHA512 0
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DARBORIST_BUILD_TESTS=OFF
        -DARBORIST_BUILD_EXAMPLES=OFF
        -DARBORIST_BUILD_BENCHMARKS=OFF
        -DARBORIST_BUILD_TOOLS=OFF
        -DARBORIST_INSTALL=ON
        -DENABLE_CLANG_TIDY=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_integration()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${SOURCE_PATH}/README.md"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)
