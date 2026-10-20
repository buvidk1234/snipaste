cmake_minimum_required(VERSION 3.28)

project(snipaste
    VERSION 0.1.6
    DESCRIPTION "Open source screenshot, pin and annotation tool"
    LANGUAGES CXX
)

set(SNIPASTE_RELEASE_VERSION "" CACHE STRING "Release version, including an optional prerelease suffix")
if(SNIPASTE_RELEASE_VERSION STREQUAL "")
    set(SNIPASTE_RELEASE_VERSION "${PROJECT_VERSION}")
endif()
if(NOT SNIPASTE_RELEASE_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?$")
    message(FATAL_ERROR "Invalid release version: ${SNIPASTE_RELEASE_VERSION}")
endif()
string(REGEX REPLACE "-.*$" "" snipaste_base_version "${SNIPASTE_RELEASE_VERSION}")
if(NOT snipaste_base_version STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR "Release version ${SNIPASTE_RELEASE_VERSION} must match project VERSION ${PROJECT_VERSION}")
endif()
if(SNIPASTE_RELEASE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+-(.+)$")
    string(REPLACE "." ";" snipaste_prerelease_identifiers "${CMAKE_MATCH_1}")
    foreach(identifier IN LISTS snipaste_prerelease_identifiers)
        if(identifier MATCHES "^0[0-9]+$")
            message(FATAL_ERROR "Numeric prerelease identifiers cannot have leading zeroes")
        endif()
    endforeach()
endif()
set(SNIPASTE_UPDATE_REPOSITORY "buvidk1234/snipaste" CACHE STRING "GitHub repository hosting update releases")

# Keep Ninja header tracking correct with localized MSVC console output.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(snipaste_dependency_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/snipaste-dependency-probe")
    file(MAKE_DIRECTORY "${snipaste_dependency_probe_dir}")
    file(WRITE "${snipaste_dependency_probe_dir}/snipaste-dependency-probe.h" "#pragma once\n")
    file(WRITE "${snipaste_dependency_probe_dir}/probe.cpp" "#include \"snipaste-dependency-probe.h\"\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VSLANG=1033"
            "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /EP
            "${snipaste_dependency_probe_dir}/probe.cpp"
        OUTPUT_VARIABLE snipaste_dependency_probe_output
        ERROR_VARIABLE snipaste_dependency_probe_error
        RESULT_VARIABLE snipaste_dependency_probe_result
        ENCODING AUTO
    )
    string(REGEX MATCH "(^|[\r\n])([^\r\n]*:)[ \t]+[^\r\n]*snipaste-dependency-probe[.]h"
        snipaste_dependency_probe_match "${snipaste_dependency_probe_output}${snipaste_dependency_probe_error}")
    if(NOT snipaste_dependency_probe_result EQUAL 0 OR NOT snipaste_dependency_probe_match)
        message(FATAL_ERROR "Could not detect the MSVC /showIncludes prefix for Ninja dependency tracking.")
    endif()
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}")
    list(PREPEND CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND}" -E env "VSLANG=1033")
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
if(APPLE)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()
# The project uses headers, not C++20 modules. Avoid a separate compiler
# dependency-scan invocation for each translation unit.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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
    src/core/recognition/ocr_image.cpp
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
    src/ui/annotation/annotation_options_sync.cpp
    src/ui/annotation/annotation_toolbar_icons.cpp
    src/ui/paste/paste_manager_persistence.cpp
    src/ui/paste/paste_window_state.cpp
    src/ui/annotation/annotation_canvas_text.cpp
    src/ui/paste/paste_window_interaction.cpp
    src/ui/paste/paste_window_actions.cpp
    src/ui/paste/paste_window_editor.cpp
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
    src/ui/annotation/annotation_canvas_style.cpp
    src/ui/annotation/annotation_canvas_selection.cpp
    src/ui/annotation/annotation_selection_geometry.cpp
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
    src/ui/snip/capture_target_worker.cpp
    src/ui/snip/capture_icons.cpp
    src/ui/snip/snip_toolbar.cpp
    src/ui/snip/snip_controls.cpp
    src/ui/snip/snip_selection.cpp
    src/ui/snip/snip_ocr.cpp
    src/ui/recognition/ocr_overlay.cpp
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
    src/core/recognition/ocr_image.h
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
    src/ui/snip/capture_target_worker.h
    src/ui/recognition/ocr_overlay.h
    src/ui/snip/capture_icons.h
    src/ui/snip/window_capture_overlay.h
    src/ui/snip/scroll_capture_panel.h
    src/ui/snip/scroll_capture_preview.h
    src/ui/tray/tray_controller.h
    src/ui/update/update_controller.h
    src/ui/update/update_installer.h
    src/ui/annotation/annotation_canvas.h
    src/ui/annotation/annotation_selection_geometry.h
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
        "-framework ScreenCaptureKit"
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
# Parse stable Qt headers once for the UI translation units.
target_precompile_headers(snipaste_ui PRIVATE <QtWidgets/QtWidgets>)

qt_add_resources(snipaste_ui "update_scripts"
    PREFIX "/update"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    FILES scripts/install-windows.ps1 scripts/install-macos.sh
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

if(WIN32)
    # This is a tray application. Do not create a console window when launched
    # by double-clicking the executable in Explorer.
    set_target_properties(snipaste PROPERTIES WIN32_EXECUTABLE TRUE)
    enable_language(RC)
    configure_file(resources/windows/version.rc.in snipaste-version.rc @ONLY)
    target_sources(snipaste PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/snipaste-version.rc")
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
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
)

# Deploy the same self-contained installation for cmake --install and packages.
set(snipaste_deploy_options)
if(WIN32)
    install(CODE "set(QT_DEPLOY_BIN_DIR .)\nset(QT_DEPLOY_PLUGINS_DIR .)")
    list(APPEND snipaste_deploy_options --no-opengl-sw --no-system-d3d-compiler)
elseif(APPLE)
    list(APPEND snipaste_deploy_options -codesign=-)
endif()
qt_generate_deploy_app_script(
    TARGET snipaste
    OUTPUT_SCRIPT snipaste_deploy_script
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
    DEPLOY_TOOL_OPTIONS ${snipaste_deploy_options}
)
install(SCRIPT "${snipaste_deploy_script}")
if(MSVC)
    # Bundle the redistributable DLLs so portable builds need no separate setup.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    include(InstallRequiredSystemLibraries)
endif()

get_filename_component(snipaste_qt_root "${Qt6_DIR}/../../.." ABSOLUTE)
add_custom_target(run
    COMMAND "${CMAKE_COMMAND}" -E env
        "QT_PLUGIN_PATH=${snipaste_qt_root}/plugins"
        --modify "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        -- "$<TARGET_FILE:snipaste>"
    DEPENDS snipaste
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:snipaste>"
    COMMENT "Running Snipaste (exit from the tray menu to finish)"
    USES_TERMINAL VERBATIM
)
# Generate the packaging script into the build tree from this build definition.
set(snipaste_package_script [=[
cmake_minimum_required(VERSION 3.28)

if(NOT "$<CONFIG>" STREQUAL "Release")
    message(FATAL_ERROR "Packages require a Release build")
endif()

set(stage "@CMAKE_CURRENT_BINARY_DIR@/package-stage")
set(dist "@CMAKE_CURRENT_SOURCE_DIR@/dist")
set(version "@SNIPASTE_RELEASE_VERSION@")
if(WIN32)
    find_program(makensis NAMES makensis.exe HINTS "$ENV{NSIS_ROOT}" REQUIRED)
    set(runtime_libraries "@CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS@")
    if(NOT runtime_libraries)
        message(FATAL_ERROR "MSVC redistributable libraries were not found during configuration")
    endif()
    set(name "snipaste-${version}-windows-x64")
    set(archive "@CMAKE_CURRENT_BINARY_DIR@/package-payload.zip")
elseif(APPLE)
    set(name "snipaste-${version}-macos-arm64")
    set(archive "${dist}/${name}.dmg")
else()
    message(FATAL_ERROR "Packages support Windows and macOS")
endif()

# Replace only the staging directory owned by this build.
file(REMOVE_RECURSE "${stage}")
file(MAKE_DIRECTORY "${stage}" "${dist}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "@CMAKE_CURRENT_BINARY_DIR@"
    --config "$<CONFIG>" --prefix "${stage}"
    COMMAND_ERROR_IS_FATAL ANY)
if(WIN32)
    file(WRITE "${stage}/README.txt" "Snipaste ${version}\nF1: screenshot. F3: paste.\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${archive}" --format=zip -- .
        WORKING_DIRECTORY "${stage}" COMMAND_ERROR_IS_FATAL ANY)

    file(GLOB_RECURSE entries LIST_DIRECTORIES TRUE RELATIVE "${stage}" "${stage}/*")
    list(SORT entries ORDER DESCENDING)
    set(uninstall_files "@CMAKE_CURRENT_BINARY_DIR@/uninstall-files.nsh")
    file(WRITE "${uninstall_files}" "")
    set(total_size 0)
    foreach(entry IN LISTS entries)
        if(IS_DIRECTORY "${stage}/${entry}")
            set(command RMDir)
        else()
            set(command Delete)
            file(SIZE "${stage}/${entry}" size)
            math(EXPR total_size "${total_size} + ${size}")
        endif()
        string(REPLACE "/" "\\" entry "${entry}")
        string(REPLACE "$" "$$" entry "${entry}")
        string(REPLACE "\"" "$\\\"" entry "${entry}")
        file(APPEND "${uninstall_files}" "${command} \"$INSTDIR\\${entry}\"\n")
    endforeach()
    math(EXPR installed_size "(${total_size} + 1023) / 1024")
    set(installer "${dist}/${name}-setup.exe")
    set(script_directory "@CMAKE_CURRENT_SOURCE_DIR@/scripts")
    foreach(path IN ITEMS archive installer uninstall_files script_directory)
        cmake_path(NATIVE_PATH ${path} nsis_${path})
    endforeach()
    execute_process(COMMAND "${makensis}" /V2 /INPUTCHARSET UTF8
        "/DVERSION=${version}" "/DPRODUCT_VERSION=@PROJECT_VERSION@.0"
        "/DARCHIVE=${nsis_archive}" "/DOUTPUT_FILE=${nsis_installer}"
        "/DSCRIPT_DIRECTORY=${nsis_script_directory}"
        "/DUNINSTALL_FILES=${nsis_uninstall_files}" "/DINSTALLED_SIZE=${installed_size}"
        "@CMAKE_CURRENT_SOURCE_DIR@/scripts/installer.nsi"
        COMMAND_ERROR_IS_FATAL ANY)
    set(packages "${installer}")
else()
    set(app "${stage}/snipaste.app")
    if(NOT EXISTS "${app}/Contents/Info.plist")
        message(FATAL_ERROR "macOS bundle Info.plist is missing; reconfigure the build before packaging")
    endif()
    execute_process(COMMAND lipo -archs "${app}/Contents/MacOS/snipaste"
        OUTPUT_VARIABLE architectures OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(NOT architectures STREQUAL "arm64")
        message(FATAL_ERROR "macOS packages require an arm64-only build; reconfigure with the macos-release preset")
    endif()
    execute_process(COMMAND codesign --force --deep --sign - "${app}"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND codesign --verify --deep --strict "${app}"
        COMMAND_ERROR_IS_FATAL ANY)
    file(CREATE_LINK "/Applications" "${stage}/Applications" SYMBOLIC)
    execute_process(COMMAND hdiutil create -volname Snipaste -srcfolder "${stage}"
        -fs HFS+ -format UDZO -ov "${archive}"
        COMMAND_ERROR_IS_FATAL ANY)
    set(packages "${archive}")
endif()

foreach(package IN LISTS packages)
    file(SHA256 "${package}" digest)
    get_filename_component(filename "${package}" NAME)
    file(WRITE "${package}.sha256" "${digest} *${filename}\n")
    message(STATUS "Package: ${package}")
endforeach()
]=])
string(CONFIGURE "${snipaste_package_script}" snipaste_package_script @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Package-$<CONFIG>.cmake"
    CONTENT "${snipaste_package_script}")
add_custom_target(package
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/Package-$<CONFIG>.cmake"
    DEPENDS snipaste
    USES_TERMINAL VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    qt_add_executable(snipaste_single_instance_tests tests/single_instance_test.cpp)
    target_link_libraries(snipaste_single_instance_tests PRIVATE snipaste_platform Qt6::Test Qt6::Concurrent)
    snipaste_apply_compile_options(snipaste_single_instance_tests)
    add_test(NAME single_instance COMMAND snipaste_single_instance_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/single-instance-test-results.txt,txt")
    set_tests_properties(single_instance PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 15)

    if(APPLE)
        qt_add_executable(macos_platform_test tests/macos_platform_test.mm)
        target_link_libraries(macos_platform_test PRIVATE snipaste_platform Qt6::Test)
        snipaste_apply_compile_options(macos_platform_test)
        add_test(NAME macos_platform_test COMMAND macos_platform_test)
        set_tests_properties(macos_platform_test PROPERTIES TIMEOUT 30)

        qt_add_executable(snipaste_macos_scroll_tests tests/macos_scroll_capture_test.mm)
        target_link_libraries(snipaste_macos_scroll_tests PRIVATE snipaste_ui Qt6::Test)
        snipaste_apply_compile_options(snipaste_macos_scroll_tests)
        add_test(NAME macos_scroll_capture COMMAND snipaste_macos_scroll_tests)
        set_tests_properties(macos_scroll_capture PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=cocoa"
            RUN_SERIAL TRUE
            TIMEOUT 60)

        qt_add_executable(snipaste_macos_capture_activation_tests tests/macos_capture_activation_test.mm)
        target_link_libraries(snipaste_macos_capture_activation_tests PRIVATE snipaste_ui Qt6::Test)
        snipaste_apply_compile_options(snipaste_macos_capture_activation_tests)
        add_test(NAME macos_capture_activation COMMAND snipaste_macos_capture_activation_tests)
        set_tests_properties(macos_capture_activation PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=cocoa"
            RUN_SERIAL TRUE
            TIMEOUT 45)
    endif()

    qt_add_executable(snipaste_paste_editor_tests tests/paste_editor_test.cpp)
    target_link_libraries(snipaste_paste_editor_tests PRIVATE snipaste_ui Qt6::Test)
    snipaste_apply_compile_options(snipaste_paste_editor_tests)
    add_test(NAME paste_editor COMMAND snipaste_paste_editor_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/paste-editor-results.txt,txt")
    set_tests_properties(paste_editor PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SNIPASTE_TEST_ARTIFACTS=${CMAKE_CURRENT_BINARY_DIR}/qa"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 60)
    foreach(scale IN ITEMS 1.25 1.5 2)
        add_test(NAME paste_editor_dpi_${scale} COMMAND snipaste_paste_editor_tests
            -o "${CMAKE_CURRENT_BINARY_DIR}/paste-editor-results-${scale}.txt,txt")
        set_tests_properties(paste_editor_dpi_${scale} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${scale}"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            TIMEOUT 60)
    endforeach()

    qt_add_executable(snipaste_application_tests tests/application_controller_test.cpp)
    target_link_libraries(snipaste_application_tests PRIVATE snipaste_app Qt6::Test)
    snipaste_apply_compile_options(snipaste_application_tests)
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
    if(WIN32 OR APPLE)
        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(Python3_FOUND)
            if(APPLE)
                set(snipaste_installer_test macos_update_installer_test.py)
            else()
                set(snipaste_installer_test update_installer_test.py)
            endif()
            add_test(NAME update_installer COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/${snipaste_installer_test}")
            set_tests_properties(update_installer PROPERTIES TIMEOUT 90)
        endif()
    endif()

    qt_add_executable(snipaste_ui_tests
        tests/ui_regression_test.cpp
    )
    target_include_directories(snipaste_ui_tests PRIVATE src)
    target_link_libraries(snipaste_ui_tests PRIVATE snipaste_ui Qt6::Test)
    qt_add_executable(snipaste_ocr_tests tests/ocr_overlay_test.cpp)
    target_link_libraries(snipaste_ocr_tests PRIVATE snipaste_ui Qt6::Test)
    snipaste_apply_compile_options(snipaste_ocr_tests)
    add_test(NAME ocr_overlay COMMAND snipaste_ocr_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/ocr-test-results.txt,txt")
    set_tests_properties(ocr_overlay PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 60)
    if(WIN32)
        set_property(TEST ocr_overlay APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
    endif()
    qt_add_executable(snipaste_annotation_selection_tests tests/annotation_selection_test.cpp)
    target_link_libraries(snipaste_annotation_selection_tests PRIVATE snipaste_ui Qt6::Test)
    snipaste_apply_compile_options(snipaste_annotation_selection_tests)
    add_test(NAME annotation_selection COMMAND snipaste_annotation_selection_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/annotation-selection-test-results.txt,txt")
    set_tests_properties(annotation_selection PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 60)
    qt_add_executable(snipaste_annotation_text_tests tests/annotation_text_test.cpp)
    target_link_libraries(snipaste_annotation_text_tests PRIVATE snipaste_ui Qt6::Test)
    snipaste_apply_compile_options(snipaste_annotation_text_tests)
    add_test(NAME annotation_text COMMAND snipaste_annotation_text_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/annotation-text-test-results.txt,txt")
    set_tests_properties(annotation_text PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 60)
    if(WIN32)
        set_property(TEST annotation_text APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
    endif()
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
