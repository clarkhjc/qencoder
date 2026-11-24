# 测试目标复用 qencoder_core，避免重复编译业务代码。
# 测试入口在各自的 .cpp 中提供，不链接 src/main.cpp。

qt_add_executable(smoke_test smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE qencoder_core)
add_test(NAME smoke_test COMMAND smoke_test)

qt_add_executable(gui_check gui_check.cpp)
target_link_libraries(gui_check PRIVATE qencoder_core)
add_dependencies(gui_check copy_themes)
add_test(NAME gui_check COMMAND gui_check)

# GUI 测试无需真实显示器，可在无头 CI 上运行
set_tests_properties(gui_check PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# 在 build 根目录运行，ThemeManager 才能找到 copy_themes 生成的主题目录
set_tests_properties(smoke_test gui_check PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
