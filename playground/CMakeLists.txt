add_executable(bt_playground playground.cpp)
target_link_libraries(bt_playground PRIVATE bt_framework)

# Playground is illustrative — exempt from strict clang-tidy enforcement.
set_target_properties(bt_playground PROPERTIES CXX_CLANG_TIDY "")
