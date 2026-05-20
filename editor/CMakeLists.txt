add_executable(bt_editor editor.cpp)
target_link_libraries(bt_editor PRIVATE bt_framework)

# Editor launcher is illustrative — exempt from strict clang-tidy enforcement.
set_target_properties(bt_editor PROPERTIES CXX_CLANG_TIDY "")
