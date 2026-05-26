set(VCPKG_BUILD_TYPE release)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO doryunger/arborist
    REF acfee89550f5c4124a0ad6e87c7e190a4a66d67f
    SHA512 9e9103b58cc57e86958483e2c6dcbda242540181a048a224beaebaaae3582dcea7b5f87f142dfb7f3014f5b44b292e34f4677fa7d0656073259d13b14bb8fba4
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
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/arborist-poc)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${SOURCE_PATH}/README.md"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)
