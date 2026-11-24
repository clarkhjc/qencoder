cmake_minimum_required(VERSION 3.16)
project(QEncoder VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg Sql)

# 统一开启 AUTOMOC / AUTOUIC 等 Qt 默认构建规则
qt_standard_project_setup()
# qt_standard_project_setup 不会自动开启 AUTORCC，这里显式打开以编译 icons.qrc
set(CMAKE_AUTORCC ON)

# 显式维护源文件清单（含 .ui 和资源），新增/删除文件时在此同步修改
set(QENCODER_CORE_SOURCES
    # core
    src/core/EncoderRegistry.cpp
    src/core/EncoderRegistry.hpp
    src/core/EnvironmentCheck.cpp
    src/core/EnvironmentCheck.hpp
    src/core/MediaProbe.cpp
    src/core/MediaProbe.hpp
    src/core/TaskQueue.cpp
    src/core/TaskQueue.hpp
    src/core/TaskStore.cpp
    src/core/TaskStore.hpp
    src/core/TranscodePreset.hpp
    src/core/TranscodeTask.hpp
    src/core/TranscodeWorker.cpp
    src/core/TranscodeWorker.hpp
    # presets
    src/presets/PresetManager.cpp
    src/presets/PresetManager.hpp
    # settings
    src/settings/Settings.cpp
    src/settings/Settings.hpp
    src/settings/ThemeManager.cpp
    src/settings/ThemeManager.hpp
    # ui
    src/ui/AddTasksDialog.cpp
    src/ui/AddTasksDialog.hpp
    src/ui/AddTasksDialog.ui
    src/ui/IconFactory.cpp
    src/ui/IconFactory.hpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.hpp
    src/ui/MainWindow.ui
    src/ui/PresetDialog.cpp
    src/ui/PresetDialog.hpp
    src/ui/PresetDialog.ui
    src/ui/PresetEditor.ui
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.hpp
    src/ui/SettingsDialog.ui
    # resources
    icons.qrc)

# main.cpp 不进公共库：主程序和测试各自提供入口点
add_library(qencoder_core OBJECT ${QENCODER_CORE_SOURCES})
target_include_directories(qencoder_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qencoder_core PUBLIC Qt6::Core Qt6::Widgets Qt6::Svg Qt6::Sql)

# macOS 默认产出 QEncoder.app；其他平台仍为普通可执行文件
option(QENCODER_MACOS_BUNDLE "在 macOS 上构建 .app 应用包" ON)

if(APPLE AND QENCODER_MACOS_BUNDLE)
    qt_add_executable(qencoder MACOSX_BUNDLE src/main.cpp)
    target_link_libraries(qencoder PRIVATE qencoder_core)

    # 应用图标 + 主题文件放入 bundle 的 Contents/Resources
    set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icons/QEncoder.icns")
    set(APP_THEMES
        "${CMAKE_CURRENT_SOURCE_DIR}/themes/dark.qss"
        "${CMAKE_CURRENT_SOURCE_DIR}/themes/light.qss"
        "${CMAKE_CURRENT_SOURCE_DIR}/themes/pink.qss")
    set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_source_files_properties(${APP_THEMES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/themes")
    target_sources(qencoder PRIVATE ${APP_ICON} ${APP_THEMES})

    set_target_properties(qencoder PROPERTIES
        OUTPUT_NAME "QEncoder"
        MACOSX_BUNDLE_BUNDLE_NAME "QEncoder"
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.qencoder"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Local"
        MACOSX_BUNDLE_ICON_FILE "QEncoder.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in")
else()
    qt_add_executable(qencoder src/main.cpp)
    target_link_libraries(qencoder PRIVATE qencoder_core)
endif()

# 把 themes/ 复制到 build 目录，便于普通可执行文件与测试查找
add_custom_target(copy_themes ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/themes ${CMAKE_BINARY_DIR}/themes
    DEPENDS themes/dark.qss themes/light.qss themes/pink.qss)
add_dependencies(qencoder copy_themes)

# 测试目标放在 tests/ 子目录中，便于后续独立扩展
option(QENCODER_BUILD_TESTS "构建测试" ON)
if(QENCODER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(APPLE AND QENCODER_MACOS_BUNDLE)
    install(TARGETS qencoder BUNDLE DESTINATION .)
else()
    install(TARGETS qencoder RUNTIME DESTINATION bin)
    # ThemeManager 会查找可执行文件同级的 themes/ 目录
    install(DIRECTORY themes DESTINATION bin)
endif()

# ---- macOS 分发辅助目标 ----
# 需要先安装 Qt 自带的 macdeployqt（Homebrew Qt 默认已提供）。
# 签名身份与公证凭据属于本机敏感信息，通过 CMake 缓存变量传入，不写入仓库。
if(APPLE AND QENCODER_MACOS_BUNDLE)
    set(QENCODER_MACOS_SIGN_IDENTITY "" CACHE STRING
        "macOS 代码签名身份（如 Developer ID Application: ...）；留空则仅做 ad-hoc 签名")
    set(QENCODER_NOTARY_PROFILE "" CACHE STRING
        "notarytool 的 keychain profile 名称；与签名身份同时设置时启用 notarize_macos")

    find_program(QENCODER_MACDEPLOYQT NAMES macdeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(QENCODER_MACDEPLOYQT)
        set(_qencoder_macdeploy_args -dmg)
        if(QENCODER_MACOS_SIGN_IDENTITY)
            list(APPEND _qencoder_macdeploy_args
                 "-sign-for-notarization=${QENCODER_MACOS_SIGN_IDENTITY}")
        endif()

        add_custom_target(deploy_macos
            COMMAND "${QENCODER_MACDEPLOYQT}"
                    "$<TARGET_BUNDLE_DIR:qencoder>" ${_qencoder_macdeploy_args}
            DEPENDS qencoder
            COMMENT "macdeployqt：打包 Qt 依赖并生成 QEncoder.dmg"
            VERBATIM)

        if(QENCODER_MACOS_SIGN_IDENTITY AND QENCODER_NOTARY_PROFILE)
            add_custom_target(notarize_macos
                COMMAND xcrun notarytool submit
                        "${CMAKE_CURRENT_BINARY_DIR}/QEncoder.dmg"
                        --keychain-profile "${QENCODER_NOTARY_PROFILE}" --wait
                COMMAND xcrun stapler staple "${CMAKE_CURRENT_BINARY_DIR}/QEncoder.dmg"
                DEPENDS deploy_macos
                COMMENT "提交 Apple 公证并装订票据"
                VERBATIM)
        elseif(QENCODER_NOTARY_PROFILE)
            message(WARNING "已设置 QENCODER_NOTARY_PROFILE，但未设置 QENCODER_MACOS_SIGN_IDENTITY，notarize_macos 不会启用")
        endif()
    else()
        message(STATUS "未找到 macdeployqt，跳过 deploy_macos 目标")
    endif()
endif()
