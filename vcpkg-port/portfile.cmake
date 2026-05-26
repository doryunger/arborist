set(VCPKG_BUILD_TYPE release)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO doryunger/arborist
    REF 68a4449b9f61a216ce191a00aa3e7036db79e2dd
    SHA512 ebe37a66bc69549e052255b8f5b18434872edcb7a527bc88a4e2097471f5998f1d9cc7f6f05ad4de04d394d4422abe039f7be0eadd8517bca7efe0ed36e08ce7
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
