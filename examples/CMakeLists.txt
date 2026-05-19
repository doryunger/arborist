find_package(GTest QUIET)

add_executable(bt_smoke smoke.cpp)
target_link_libraries(bt_smoke PRIVATE bt_framework)
