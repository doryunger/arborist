add_executable(bt_smoke smoke.cpp)
target_link_libraries(bt_smoke PRIVATE bt_framework)

add_executable(bt_demo demo.cpp)
target_link_libraries(bt_demo PRIVATE bt_framework)

# Examples are illustrative — exempt from strict clang-tidy enforcement.
set_target_properties(bt_smoke bt_demo PROPERTIES CXX_CLANG_TIDY "")
