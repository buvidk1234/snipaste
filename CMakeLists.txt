cmake_minimum_required(VERSION 3.28)

project(snipaste
    VERSION 0.1.1
    DESCRIPTION "Open source screenshot, pin and annotation tool"
    LANGUAGES CXX
)

set(SNIPASTE_RELEASE_VERSION "" CACHE STRING "Release version, including an optional prerelease suffix")
if(SNIPASTE_RELEASE_VERSION STREQUAL "")
    set(SNIPASTE_RELEASE_VERSION "${PROJECT_VERSION}")
endif()
set(SNIPASTE_UPDATE_REPOSITORY "buvidk1234/snipaste" CACHE STRING "GitHub repository hosting update releases")

if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    # CMake can misdecode the localized MSVC /showIncludes prefix when the
    # configure process uses UTF-8.  Keep Ninja's dependency scanner quiet
    # while retaining compiler dependency tracking for the Chinese toolchain.
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()

if(APPLE)
    # Carbon, AppKit, CoreGraphics, Accessibility and Vision are exposed
    # through Objective-C APIs. Keep those bridges isolated in .mm files so
    # the Windows target remains ordinary C++/Win32.
    enable_language(OBJCXX)
    set(SNIPASTE_NATIVE_PLATFORM_SOURCES
        src/platform/macos/global_hotkey_macos.mm
        src/platform/macos/super_capture_macos.mm
        src/platform/macos/capture_target_macos.mm
    )
else()
    set(SNIPASTE_NATIVE_PLATFORM_SOURCES
        src/platform/windows/global_hotkey.cpp
        src/platform/windows/super_capture.cpp
        src/platform/windows/capture_target.cpp
    )
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent PrintSupport)

set(SNIPASTE_CORE_SOURCES
    src/core/paste/image_transform.cpp
    src/core/paste/paste_state_store.cpp
    src/core/annotation/annotation_types.cpp
    src/core/annotation/annotation_document.cpp
    src/core/command/argument_parser.cpp
    src/core/command/command_registry.cpp
    src/core/config/config_store.cpp
    src/core/history/screenshot_history.cpp
    src/core/history/screenshot_history_writer.cpp
    src/core/scroll_stitcher.cpp
    src/core/scroll_frame_matcher.cpp
    src/core/recognition/ocr_service.cpp
    src/core/recognition/barcode_service.cpp
    src/core/update/release_info.cpp
    src/core/update/github_update_client.cpp
)

set(SNIPASTE_PLATFORM_SOURCES
    src/platform/startup_integration.cpp
    src/platform/desktop_integration.cpp
    ${SNIPASTE_NATIVE_PLATFORM_SOURCES}
    src/platform/screen_capture.cpp
    src/platform/windows/single_instance.cpp
)

set(SNIPASTE_UI_SOURCES
    src/ui/annotation/annotation_options_geometry.cpp
    src/ui/annotation/annotation_options_brushes.cpp
    src/ui/annotation/annotation_options_effects.cpp
    src/ui/annotation/annotation_options_text.cpp
    src/ui/annotation/annotation_options.cpp
    src/ui/annotation/annotation_toolbar_icons.cpp
    src/ui/paste/paste_manager_persistence.cpp
    src/ui/paste/paste_window_state.cpp
    src/ui/annotation/annotation_canvas_text.cpp
    src/ui/paste/paste_window_interaction.cpp
    src/ui/paste/paste_window_actions.cpp
    src/ui/paste/paste_window_drop.cpp
    src/ui/paste/paste_group_dialog.cpp
    src/ui/whiteboard/whiteboard_window.cpp
    src/ui/snip/capture_support.cpp
    src/ui/output/clipboard_image_data.cpp
    src/ui/output/image_output.cpp
    src/ui/paste/clipboard_content.cpp
    src/ui/paste/paste_window.cpp
    src/ui/paste/paste_manager.cpp
    src/ui/annotation/annotation_dialog.cpp
    src/ui/annotation/annotation_canvas.cpp
    src/ui/annotation/annotation_canvas_interaction.cpp
    src/ui/annotation/annotation_canvas_render.cpp
    src/ui/annotation/annotation_renderer.cpp
    src/ui/annotation/annotation_effect_cache.cpp
    src/ui/annotation/annotation_stroke_preview.cpp
    src/ui/annotation/image_region.cpp
    src/ui/history/screenshot_history_dialog.cpp
    src/ui/preferences/preferences_dialog.cpp
    src/ui/preferences/preference_widgets.cpp
    src/ui/preferences/preferences_paste.cpp
    src/ui/preferences/preferences_output.cpp
    src/ui/preferences/preferences_control.cpp
    src/ui/preferences/preferences_update.cpp
    src/ui/preferences/preferences_about.cpp
    src/ui/preferences/preferences_appearance.cpp
    src/ui/preferences/preferences_capture.cpp
    src/ui/preferences/appearance_settings.cpp
    src/ui/snip/snip_overlay.cpp
    src/ui/snip/capture_icons.cpp
    src/ui/snip/snip_toolbar.cpp
    src/ui/snip/snip_controls.cpp
    src/ui/snip/snip_selection.cpp
    src/ui/snip/window_capture_overlay.cpp
    src/ui/snip/scroll_capture_panel.cpp
    src/ui/snip/scroll_capture_preview.cpp
    src/ui/snip/snip_scroll_capture.cpp
    src/ui/tray/tray_controller.cpp
    src/ui/update/update_controller.cpp
    src/ui/update/update_installer.cpp
)

if(APPLE)
    list(APPEND SNIPASTE_PLATFORM_SOURCES
        src/platform/macos/vision_ocr.mm
        src/platform/macos/system.mm
    )
endif()

set(SNIPASTE_APP_HEADERS
    src/app/application_controller.h
    src/app/capture_controller.h
    src/app/command_line.h
    src/app/input_controller.h
    src/app/screenshot_history_service.h
    src/app/screenshot_output.h
    src/app/external_command.h
    src/app/runtime_settings.h
    src/app/logging.h
)

set(SNIPASTE_CORE_HEADERS
    src/core/paste/image_transform.h
    src/core/paste/paste_state_store.h
    src/core/command/argument_parser.h
    src/core/command/command_registry.h
    src/core/config/config_store.h
    src/core/control/hot_corner_state.h
    src/core/history/screenshot_history.h
    src/core/history/screenshot_history_writer.h
    src/core/scroll_stitcher.h
    src/core/scroll_frame_matcher.h
    src/core/scroll_capture_stability.h
    src/core/recognition/ocr_service.h
    src/core/recognition/barcode_service.h
    src/core/update/release_info.h
    src/core/update/github_update_client.h
    src/core/annotation/annotation_types.h
    src/core/annotation/annotation_document.h
)

set(SNIPASTE_PLATFORM_HEADERS
    src/platform/startup_integration.h
    src/platform/desktop_integration.h
    src/platform/windows/global_hotkey.h
    src/platform/windows/super_capture.h
    src/platform/windows/capture_target.h
    src/platform/windows/single_instance.h
    src/platform/keyboard.h
    src/platform/screen_capture.h
)

set(SNIPASTE_UI_HEADERS
    src/ui/annotation/annotation_options.h
    src/ui/annotation/annotation_toolbar_icons.h
    src/ui/annotation/annotation_option_widgets.h
    src/ui/preferences/preference_widgets.h
    src/ui/paste/paste_group_dialog.h
    src/ui/whiteboard/whiteboard_window.h
    src/ui/snip/capture_support.h
    src/ui/output/image_output.h
    src/ui/output/clipboard_image_data.h
    src/ui/paste/clipboard_content.h
    src/ui/paste/paste_window.h
    src/ui/paste/paste_manager.h
    src/ui/clipboard_output.h
    src/ui/annotation/annotation_dialog.h
    src/ui/annotation/image_region.h
    src/ui/history/screenshot_history_dialog.h
    src/ui/preferences/preferences_dialog.h
    src/ui/preferences/appearance_settings.h
    src/ui/snip/snip_overlay.h
    src/ui/snip/capture_icons.h
    src/ui/snip/window_capture_overlay.h
    src/ui/snip/scroll_capture_panel.h
    src/ui/snip/scroll_capture_preview.h
    src/ui/tray/tray_controller.h
    src/ui/update/update_controller.h
    src/ui/update/update_installer.h
    src/ui/annotation/annotation_canvas.h
    src/ui/annotation/annotation_renderer.h
    src/ui/annotation/annotation_effect_cache.h
    src/ui/annotation/annotation_stroke_preview.h
)

if(APPLE)
    list(APPEND SNIPASTE_PLATFORM_HEADERS
        src/platform/macos/capture.h
        src/platform/macos/vision_ocr.h
        src/platform/macos/system.h
    )
endif()

set(SNIPASTE_HEADERS
    ${SNIPASTE_APP_HEADERS}
    ${SNIPASTE_CORE_HEADERS}
    ${SNIPASTE_PLATFORM_HEADERS}
    ${SNIPASTE_UI_HEADERS}
)

set(SNIPASTE_APP_SOURCES
    src/app/application_arguments.cpp
    src/app/application_capture_commands.cpp
    src/app/application_controller.cpp
    src/app/application_paste_commands.cpp
    src/app/application_utility_commands.cpp
    src/app/capture_controller.cpp
    src/app/command_line.cpp
    src/app/input_controller.cpp
    src/app/screenshot_history_service.cpp
    src/app/screenshot_output.cpp
    src/app/external_command.cpp
    src/app/runtime_settings.cpp
    src/app/logging.cpp
)

set(SNIPASTE_MAC_FRAMEWORKS)
if(APPLE)
    set(SNIPASTE_MAC_FRAMEWORKS
        "-framework Carbon"
        "-framework AppKit"
        "-framework ApplicationServices"
        "-framework CoreGraphics"
        "-framework Vision"
        "-framework ImageIO"
        "-framework Foundation"
    )
endif()

# Keep the build graph aligned with the source boundaries.  The old target
# compiled every source file directly into the executable, which made an
# unrelated UI change rebuild the whole application and hid dependencies.
qt_add_library(snipaste_core STATIC
    ${SNIPASTE_CORE_SOURCES}
    ${SNIPASTE_CORE_HEADERS}
)
target_include_directories(snipaste_core PUBLIC src)
target_link_libraries(snipaste_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)

qt_add_library(snipaste_platform STATIC
    ${SNIPASTE_PLATFORM_SOURCES}
    ${SNIPASTE_PLATFORM_HEADERS}
)
target_include_directories(snipaste_platform PUBLIC src)
target_link_libraries(snipaste_platform PUBLIC Qt6::Core Qt6::Gui Qt6::Network)
if(WIN32)
    target_link_libraries(snipaste_platform PUBLIC
        user32 oleacc ole32 dwmapi uiautomationcore windowsapp)
endif()
if(APPLE)
    target_link_libraries(snipaste_platform PUBLIC ${SNIPASTE_MAC_FRAMEWORKS})
endif()

qt_add_library(snipaste_ui STATIC
    ${SNIPASTE_UI_SOURCES}
    ${SNIPASTE_UI_HEADERS}
)

qt_add_resources(snipaste_ui "update_scripts"
    PREFIX "/update"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/scripts/updates"
    FILES scripts/updates/install-windows.ps1 scripts/updates/install-macos.sh
)
target_compile_definitions(snipaste_ui PRIVATE
    SNIPASTE_UPDATE_REPOSITORY=\"${SNIPASTE_UPDATE_REPOSITORY}\")
target_include_directories(snipaste_ui PUBLIC src)
target_link_libraries(snipaste_ui
    PUBLIC
        snipaste_core
        snipaste_platform
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
        Qt6::PrintSupport
)
if(WIN32)
    target_link_libraries(snipaste_ui PUBLIC winmm)
endif()

qt_add_library(snipaste_app STATIC ${SNIPASTE_APP_SOURCES} ${SNIPASTE_APP_HEADERS})
target_include_directories(snipaste_app PUBLIC src)
target_link_libraries(snipaste_app PUBLIC snipaste_ui)
qt_add_executable(snipaste src/app/main.cpp)

if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    # Localized /showIncludes output can disagree with Ninja's detected prefix.
    # Keep explicit project-header dependencies so an incremental build never
    # mixes object files compiled against different class layouts.
    set(snipaste_header_dependencies)
    foreach(header IN LISTS SNIPASTE_HEADERS)
        list(APPEND snipaste_header_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
    endforeach()
    set_property(SOURCE src/app/main.cpp ${SNIPASTE_APP_SOURCES} ${SNIPASTE_CORE_SOURCES}
        ${SNIPASTE_PLATFORM_SOURCES} ${SNIPASTE_UI_SOURCES} tests/ui_regression_test.cpp
        tests/snip_performance_probe.cpp tests/annotation_drawing_probe.cpp tests/annotation_render_test.cpp
        APPEND PROPERTY OBJECT_DEPENDS "${snipaste_header_dependencies}")
endif()

if(WIN32)
    # This is a tray application. Do not create a console window when launched
    # by double-clicking the executable in Explorer.
    set_target_properties(snipaste PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(snipaste PRIVATE src)
target_link_libraries(snipaste PRIVATE snipaste_app)

if(WIN32)
    # Windows.Media.Ocr is a WinRT API. The Windows SDK ships the C++/WinRT
    # headers and Windows App library; using it keeps OCR available without a
    # separate Tesseract installation on normal Windows systems.
    file(GLOB SNIPASTE_CPPWINRT_DIRS "$ENV{WindowsSdkDir}/Include/*/cppwinrt")
    if(SNIPASTE_CPPWINRT_DIRS)
        list(SORT SNIPASTE_CPPWINRT_DIRS)
        list(GET SNIPASTE_CPPWINRT_DIRS -1 SNIPASTE_CPPWINRT_DIR)
        target_include_directories(snipaste_core SYSTEM PRIVATE "${SNIPASTE_CPPWINRT_DIR}")
    endif()
endif()

if(APPLE)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/snipaste-info.plist"
        @ONLY
    )
    set_target_properties(snipaste PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.snipaste.local"
        MACOSX_BUNDLE_BUNDLE_NAME "Snipaste Local"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_STRING "Snipaste Local screenshot and pin tool"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/snipaste-info.plist"
    )
endif()

target_compile_definitions(snipaste
    PRIVATE
        SNIPASTE_VERSION=\"${SNIPASTE_RELEASE_VERSION}\"
        $<$<PLATFORM_ID:Windows>:SNIPASTE_PLATFORM_WINDOWS=1>
)

function(snipaste_apply_compile_options target)
    if(MSVC)
        # UI strings are UTF-8 source text. Keep MSVC from decoding them with
        # the machine's active code page and producing mojibake at runtime.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

snipaste_apply_compile_options(snipaste_core)
snipaste_apply_compile_options(snipaste_platform)
snipaste_apply_compile_options(snipaste_ui)
snipaste_apply_compile_options(snipaste)
snipaste_apply_compile_options(snipaste_app)

install(TARGETS snipaste
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

option(SNIPASTE_BUILD_TESTS "Build UI regression tests" OFF)
if(SNIPASTE_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    qt_add_executable(snipaste_application_tests tests/application_controller_test.cpp)
    target_link_libraries(snipaste_application_tests PRIVATE snipaste_app Qt6::Test)
    snipaste_apply_compile_options(snipaste_application_tests)
    if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
        set_property(SOURCE tests/application_controller_test.cpp
            APPEND PROPERTY OBJECT_DEPENDS "${snipaste_header_dependencies}")
    endif()
    add_test(NAME application_controller COMMAND snipaste_application_tests)
    set_tests_properties(application_controller PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 45)

    qt_add_executable(snipaste_core_tests tests/core_unit_test.cpp)
    target_include_directories(snipaste_core_tests PRIVATE src)
    target_link_libraries(snipaste_core_tests PRIVATE snipaste_core Qt6::Test)
    snipaste_apply_compile_options(snipaste_core_tests)
    add_test(NAME core_unit COMMAND snipaste_core_tests)
    set_tests_properties(core_unit PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

    qt_add_executable(snipaste_update_tests tests/update_test.cpp)
    target_link_libraries(snipaste_update_tests PRIVATE snipaste_core Qt6::Test)
    snipaste_apply_compile_options(snipaste_update_tests)
    add_test(NAME updates COMMAND snipaste_update_tests)
    set_tests_properties(updates PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 30)
    if(WIN32)
        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(Python3_FOUND)
            add_test(NAME update_installer COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/update_installer_test.py")
            set_tests_properties(update_installer PROPERTIES TIMEOUT 90)
        endif()
    endif()

    qt_add_executable(snipaste_ui_tests
        tests/ui_regression_test.cpp
    )
    target_include_directories(snipaste_ui_tests PRIVATE src)
    target_link_libraries(snipaste_ui_tests PRIVATE snipaste_ui Qt6::Test)
    qt_add_executable(snipaste_annotation_render_tests tests/annotation_render_test.cpp)
    target_link_libraries(snipaste_annotation_render_tests PRIVATE snipaste_ui Qt6::Test)
    snipaste_apply_compile_options(snipaste_annotation_render_tests)
    add_test(NAME annotation_render COMMAND snipaste_annotation_render_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/annotation-render-test-results.txt,txt")
    set_tests_properties(annotation_render PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_executable(snipaste_performance_probe tests/snip_performance_probe.cpp)
    add_executable(snipaste_drawing_probe tests/annotation_drawing_probe.cpp)
    target_link_libraries(snipaste_drawing_probe PRIVATE snipaste_ui)
    snipaste_apply_compile_options(snipaste_drawing_probe)
    add_test(NAME annotation_drawing COMMAND snipaste_drawing_probe)
    set_tests_properties(annotation_drawing PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    target_link_libraries(snipaste_performance_probe PRIVATE snipaste_ui)
    target_compile_definitions(snipaste_performance_probe PRIVATE SNIPASTE_HAS_PREPARED_CAPTURE=1)
    snipaste_apply_compile_options(snipaste_performance_probe)
    if(WIN32)
        target_link_libraries(snipaste_performance_probe PRIVATE psapi)
    endif()
    if(WIN32)
        add_executable(snipaste_capture_probe tests/capture_target_probe.cpp)
        target_include_directories(snipaste_capture_probe PRIVATE src)
        target_link_libraries(snipaste_capture_probe PRIVATE snipaste_platform)
        if(MSVC)
            snipaste_apply_compile_options(snipaste_capture_probe)
            # The probe calls the public capture-target API directly.  Keep
            # its object file tied to the same project headers as the main
            # targets so removed/changed overloads cannot leave a stale
            # Release object behind during an incremental build.
            set_property(SOURCE tests/capture_target_probe.cpp
                APPEND PROPERTY OBJECT_DEPENDS "${snipaste_header_dependencies}")
        endif()
    endif()
    if(MSVC)
        target_compile_options(snipaste_ui_tests PRIVATE /utf-8 /W4 /permissive-)
    endif()
    add_test(NAME ui_regression COMMAND snipaste_ui_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/ui-test-results.txt,txt")
    get_filename_component(snipaste_qt_sdk "${Qt6_DIR}/../../.." ABSOLUTE)
    set_tests_properties(ui_regression PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${snipaste_qt_sdk}/plugins;SNIPASTE_TEST_ARTIFACTS=${CMAKE_CURRENT_BINARY_DIR}/qa"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        # The full offscreen UI suite also renders reference PNG artifacts.
        # On Windows with MSVC this can exceed 45 seconds even when every
        # interaction test passes, so avoid reporting a false timeout.
        TIMEOUT 120
    )
    foreach(scale IN ITEMS 1.25 1.5 2)
        add_test(NAME paste_dpi_${scale} COMMAND snipaste_ui_tests
            pastePreservesOriginalPixels pasteSelectionClipboardSize pasteDoubleClickRestores
            pasteCaptureOriginAndClipboardIsolation pasteClickAndDragCursor
            inlineTextInputAndCancel inlineTextOutput textEditorCleanupAndToolSwitch
            pasteRecoveryLimitAndDestruction pasteNewClipboardTakesPriority pastePersistence
            pastePersistenceReusesPixelsAndHandlesFailure annotationInvalidationReleasesPixels
            historyDialogLoadsPreviewsAsynchronously
            proGroupedToolsAndStyles proSizeRefreshAndOutput proHistoryAndSelectionShortcuts
            historyRestoresPairedGeometryAndBackground legacyHistoryUsesNeutralPreview proLayoutAndAnnotations
            -o "${CMAKE_CURRENT_BINARY_DIR}/paste-test-results-${scale}.txt,txt")
        set_tests_properties(paste_dpi_${scale} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${snipaste_qt_sdk}/plugins;QT_SCALE_FACTOR=${scale}"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            TIMEOUT 45
        )
    endforeach()
endif()
