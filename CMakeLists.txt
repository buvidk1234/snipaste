cmake_minimum_required(VERSION 3.28)

project(snipate
    VERSION 0.1.0
    DESCRIPTION "Open source screenshot, pin and annotation tool"
    LANGUAGES CXX
)

if(APPLE)
    # Carbon, AppKit, CoreGraphics, Accessibility and Vision are exposed
    # through Objective-C APIs. Keep those bridges isolated in .mm files so
    # the Windows target remains ordinary C++/Win32.
    enable_language(OBJCXX)
    set(SNIPATE_PLATFORM_SOURCES
        src/platform/macos/global_hotkey_macos.mm
        src/platform/macos/super_capture_macos.mm
        src/platform/macos/capture_target_macos.mm
    )
    set(SNIPATE_CAPTURE_TARGET_SOURCE src/platform/macos/capture_target_macos.mm)
else()
    set(SNIPATE_PLATFORM_SOURCES
        src/platform/windows/global_hotkey.cpp
        src/platform/windows/super_capture.cpp
        src/platform/windows/capture_target.cpp
    )
    set(SNIPATE_CAPTURE_TARGET_SOURCE src/platform/windows/capture_target.cpp)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent PrintSupport)

set(SNIPATE_SOURCES
    src/app/main.cpp
    src/app/logging.cpp
    src/core/command/command_registry.cpp
    src/core/config/config_store.cpp
    src/core/history/screenshot_history.cpp
    src/core/recognition/ocr_service.cpp
    src/core/recognition/barcode_service.cpp
    ${SNIPATE_PLATFORM_SOURCES}
    src/platform/windows/single_instance.cpp
    src/ui/paste/paste_window.cpp
    src/ui/paste/paste_manager.cpp
    src/ui/annotation/annotation_dialog.cpp
    src/ui/history/screenshot_history_dialog.cpp
    src/ui/preferences/preferences_dialog_rich.cpp
    src/ui/preferences/preferences_appearance.cpp
    src/ui/preferences/preferences_capture.cpp
    src/ui/preferences/appearance_settings.cpp
    src/ui/snip/snip_overlay.cpp
    src/ui/snip/capture_icons.cpp
    src/ui/snip/snip_toolbar.cpp
    src/ui/snip/snip_controls.cpp
    src/ui/snip/snip_selection.cpp
    src/ui/snip/window_capture_overlay.cpp
    src/ui/tray/tray_controller.cpp
)

if(APPLE)
    list(APPEND SNIPATE_SOURCES
        src/platform/macos/vision_ocr.mm
        src/platform/macos/system.mm
    )
endif()

set(SNIPATE_HEADERS
    src/app/logging.h
    src/core/command/command_registry.h
    src/core/config/config_store.h
    src/core/control/hot_corner_state.h
    src/core/history/screenshot_history.h
    src/core/recognition/ocr_service.h
    src/core/recognition/barcode_service.h
    src/platform/windows/global_hotkey.h
    src/platform/windows/super_capture.h
    src/platform/windows/capture_target.h
    src/platform/windows/single_instance.h
    src/platform/keyboard.h
    src/ui/paste/paste_window.h
    src/ui/paste/paste_manager.h
    src/ui/annotation/annotation_dialog.h
    src/ui/history/screenshot_history_dialog.h
    src/ui/preferences/preferences_dialog.h
    src/ui/preferences/appearance_settings.h
    src/ui/snip/snip_overlay.h
    src/ui/snip/capture_icons.h
    src/ui/snip/window_capture_overlay.h
    src/ui/tray/tray_controller.h
)

if(APPLE)
    list(APPEND SNIPATE_HEADERS
        src/platform/macos/capture.h
        src/platform/macos/vision_ocr.h
        src/platform/macos/system.h
    )
endif()

qt_add_executable(snipate ${SNIPATE_SOURCES} ${SNIPATE_HEADERS})

if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    # Localized /showIncludes output can disagree with Ninja's detected prefix.
    # Keep explicit project-header dependencies so an incremental build never
    # mixes object files compiled against different class layouts.
    set(snipate_header_dependencies)
    foreach(header IN LISTS SNIPATE_HEADERS)
        list(APPEND snipate_header_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
    endforeach()
    set_property(SOURCE ${SNIPATE_SOURCES} tests/ui_regression_test.cpp
        APPEND PROPERTY OBJECT_DEPENDS "${snipate_header_dependencies}")
endif()

if(WIN32)
    # This is a tray application. Do not create a console window when launched
    # by double-clicking the executable in Explorer.
    set_target_properties(snipate PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(snipate PRIVATE src)

target_link_libraries(snipate
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
        Qt6::PrintSupport
)

if(WIN32)
    # Windows.Media.Ocr is a WinRT API. The Windows SDK ships the C++/WinRT
    # headers and Windows App library; using it keeps OCR available without a
    # separate Tesseract installation on normal Windows systems.
    file(GLOB SNIPATE_CPPWINRT_DIRS "$ENV{WindowsSdkDir}/Include/*/cppwinrt")
    if(SNIPATE_CPPWINRT_DIRS)
        list(SORT SNIPATE_CPPWINRT_DIRS)
        list(GET SNIPATE_CPPWINRT_DIRS -1 SNIPATE_CPPWINRT_DIR)
        target_include_directories(snipate SYSTEM PRIVATE "${SNIPATE_CPPWINRT_DIR}")
    endif()
    target_link_libraries(snipate PRIVATE user32 oleacc ole32 dwmapi uiautomationcore windowsapp)
endif()

if(APPLE)
    set(SNIPATE_MAC_FRAMEWORKS
        "-framework Carbon"
        "-framework AppKit"
        "-framework ApplicationServices"
        "-framework CoreGraphics"
        "-framework Vision"
        "-framework ImageIO"
        "-framework Foundation"
    )
    target_link_libraries(snipate PRIVATE ${SNIPATE_MAC_FRAMEWORKS})
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/snipate-info.plist"
        @ONLY
    )
    set_target_properties(snipate PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.snipate.local"
        MACOSX_BUNDLE_BUNDLE_NAME "Snipate Local"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_STRING "Snipate Local screenshot and pin tool"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/snipate-info.plist"
    )
endif()

target_compile_definitions(snipate
    PRIVATE
        SNIPATE_VERSION=\"${PROJECT_VERSION}\"
        $<$<PLATFORM_ID:Windows>:SNIPATE_PLATFORM_WINDOWS=1>
)

if(MSVC)
    # UI strings are UTF-8 source text. Keep MSVC from decoding them with
    # the machine's active code page and producing mojibake at runtime.
    target_compile_options(snipate PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(snipate PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS snipate
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

option(SNIPATE_BUILD_TESTS "Build UI regression tests" OFF)
if(SNIPATE_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    set(SNIPATE_TEST_PLATFORM_SOURCES ${SNIPATE_CAPTURE_TARGET_SOURCE})
    if(APPLE)
        list(APPEND SNIPATE_TEST_PLATFORM_SOURCES src/platform/macos/system.mm)
    endif()
    qt_add_executable(snipate_ui_tests
        tests/ui_regression_test.cpp
        src/core/command/command_registry.cpp
        src/core/config/config_store.cpp
        ${SNIPATE_TEST_PLATFORM_SOURCES}
        src/ui/preferences/preferences_dialog_rich.cpp
        src/ui/preferences/preferences_appearance.cpp
        src/ui/preferences/appearance_settings.cpp
        src/ui/preferences/preferences_capture.cpp
        src/ui/annotation/annotation_dialog.cpp
        src/ui/paste/paste_window.cpp
        src/ui/paste/paste_manager.cpp
        src/ui/snip/snip_overlay.cpp
        src/ui/snip/snip_toolbar.cpp
        src/ui/snip/snip_controls.cpp
        src/ui/snip/snip_selection.cpp
        src/ui/snip/capture_icons.cpp
        src/ui/tray/tray_controller.cpp
    )
    target_include_directories(snipate_ui_tests PRIVATE src)
    target_link_libraries(snipate_ui_tests PRIVATE Qt6::Widgets Qt6::Network Qt6::Test)
    if(APPLE)
        target_link_libraries(snipate_ui_tests PRIVATE ${SNIPATE_MAC_FRAMEWORKS})
    endif()
    if(WIN32)
        target_link_libraries(snipate_ui_tests PRIVATE user32 oleacc ole32 dwmapi uiautomationcore windowsapp)
        add_executable(snipate_capture_probe tests/capture_target_probe.cpp
            src/platform/windows/capture_target.cpp)
        target_include_directories(snipate_capture_probe PRIVATE src)
        target_link_libraries(snipate_capture_probe PRIVATE Qt6::Gui user32 oleacc ole32 dwmapi uiautomationcore)
        if(MSVC)
            target_compile_options(snipate_capture_probe PRIVATE /utf-8 /W4 /permissive-)
            # The probe calls the public capture-target API directly.  Keep
            # its object file tied to the same project headers as the main
            # targets so removed/changed overloads cannot leave a stale
            # Release object behind during an incremental build.
            set_property(SOURCE tests/capture_target_probe.cpp
                APPEND PROPERTY OBJECT_DEPENDS "${snipate_header_dependencies}")
        endif()
    endif()
    if(MSVC)
        target_compile_options(snipate_ui_tests PRIVATE /utf-8 /W4 /permissive-)
    endif()
    add_test(NAME ui_regression COMMAND snipate_ui_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/ui-test-results.txt,txt")
    get_filename_component(snipate_qt_sdk "${Qt6_DIR}/../../.." ABSOLUTE)
    set_tests_properties(ui_regression PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${snipate_qt_sdk}/plugins;SNIPATE_TEST_ARTIFACTS=${CMAKE_CURRENT_BINARY_DIR}/qa"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        # The full offscreen UI suite also renders reference PNG artifacts.
        # On Windows with MSVC this can exceed 45 seconds even when every
        # interaction test passes, so avoid reporting a false timeout.
        TIMEOUT 120
    )
    foreach(scale IN ITEMS 1.25 1.5 2)
        add_test(NAME paste_dpi_${scale} COMMAND snipate_ui_tests
            pastePreservesOriginalPixels pasteSelectionClipboardSize pasteDoubleClickRestores
            pasteCaptureOriginAndClipboardIsolation pasteClickAndDragCursor
            inlineTextInputAndCancel inlineTextOutput textEditorCleanupAndToolSwitch
            pasteRecoveryLimitAndDestruction pasteNewClipboardTakesPriority pastePersistence
            proGroupedToolsAndStyles proSizeRefreshAndOutput proHistoryAndSelectionShortcuts proLayoutAndAnnotations
            -o "${CMAKE_CURRENT_BINARY_DIR}/paste-test-results-${scale}.txt,txt")
        set_tests_properties(paste_dpi_${scale} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${snipate_qt_sdk}/plugins;QT_SCALE_FACTOR=${scale}"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            TIMEOUT 45
        )
    endforeach()
endif()
