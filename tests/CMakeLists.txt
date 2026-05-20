find_package(GTest QUIET)

if(NOT GTest_FOUND)
    message(STATUS "GoogleTest not found — skipping tests")
    return()
endif()

file(GLOB TEST_SOURCES *.cpp)
add_executable(bt_tests ${TEST_SOURCES})
target_link_libraries(bt_tests PRIVATE bt_framework GTest::gtest_main httplib::httplib)

include(GoogleTest)
gtest_discover_tests(bt_tests)
