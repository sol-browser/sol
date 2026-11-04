set(CEF_TARGET sol)
SET_CEF_TARGET_OUT_DIR()
set(RED_RUNTIME_OUTPUT_DIR "" CACHE PATH "Optional isolated client runtime directory for local testing")
if(RED_RUNTIME_OUTPUT_DIR)
  set(CEF_TARGET_OUT_DIR "${RED_RUNTIME_OUTPUT_DIR}")
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CEF_TARGET_OUT_DIR}")
endif()
ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
add_library(workspace_store STATIC src/storage/tree_store.cpp src/storage/history_store.cpp)
target_include_directories(workspace_store PUBLIC src)
target_link_libraries(workspace_store PUBLIC sqlite bcrypt)
target_compile_options(workspace_store PRIVATE /W4 /WX /utf-8)
target_compile_definitions(workspace_store PUBLIC _HAS_EXCEPTIONS=0 NOMINMAX WIN32_LEAN_AND_MEAN)
set_property(TARGET workspace_store PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Other browsers' profile formats (CEF-free so the parsers are unit-tested).
add_library(import_formats STATIC src/import/formats/json_lite.cpp src/import/formats/import_crypto.cpp
  src/import/formats/password_csv.cpp src/import/formats/chromium_profile.cpp src/import/formats/firefox_logins.cpp
  src/import/formats/import_tree.cpp src/import/formats/favicons.cpp)
target_link_libraries(import_formats PUBLIC workspace_store crypt32)
target_compile_options(import_formats PRIVATE /W4 /WX /utf-8)
set_property(TARGET import_formats PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Since CEF 151 the upstream Windows sandbox uses bootstrap.exe + a client DLL.
# Keep the upstream executable byte-for-byte and export RunWinMain from our DLL.
add_library(${CEF_TARGET} SHARED src/main_win.cpp src/app/state.cpp src/app/theme.cpp
  src/experiments/tab_probe.cpp src/windows/embedded_content.cpp src/windows/page_corners.cpp src/windows/page_notification.cpp)
target_sources(${CEF_TARGET} PRIVATE src/workspace/controller.cpp src/experiments/shell_capture.cpp)
target_sources(${CEF_TARGET} PRIVATE src/workspace/folder_removal.cpp)
target_sources(${CEF_TARGET} PRIVATE src/experiments/window_transfer.cpp)
target_sources(${CEF_TARGET} PRIVATE src/app/session.cpp)
target_sources(${CEF_TARGET} PRIVATE src/app/auto_archive.cpp src/workspace/auto_archive_controller.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/link_peek_layout.cpp src/windows/link_peek_scene.cpp src/windows/auto_pip.cpp src/windows/video_pip_button.cpp src/windows/link_peek_close.cpp src/windows/link_peek_readiness.cpp src/workspace/link_peek_controller.cpp src/workspace/media_controller.cpp src/workspace/tab_menu_actions.cpp)
target_sources(${CEF_TARGET} PRIVATE src/app/preferences.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/tab_drag_preview.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/sidebar_shadow.cpp)
target_sources(${CEF_TARGET} PRIVATE src/experiments/extension_observation.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/popup_animation.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/popup_animation_host.cpp)
target_sources(${CEF_TARGET} PRIVATE src/experiments/organization_probe.cpp)
target_sources(${CEF_TARGET} PRIVATE src/experiments/input_probe.cpp)
target_sources(${CEF_TARGET} PRIVATE src/navigation/shortcuts.cpp)
target_sources(${CEF_TARGET} PRIVATE src/navigation/shortcut_settings.cpp)
target_sources(${CEF_TARGET} PRIVATE src/navigation/search_suggestions.cpp)
target_sources(${CEF_TARGET} PRIVATE src/experiments/browsing_probe.cpp
  src/experiments/fullscreen_probe.cpp
  src/experiments/permissions_probe.cpp
  src/experiments/site_probe.cpp
  src/site/site_settings.cpp
  src/site/boost_store.cpp
  src/windows/boosts.cpp
  src/windows/boost_window.cpp
  src/windows/menu_popup.cpp
  src/windows/page_menu.cpp
  src/history/history_service.cpp
  src/history/history_commands.cpp
  src/windows/main_menu.cpp
  src/renderer/audible_detector.cpp
  src/windows/window_placement.cpp
  src/downloads/downloads_page.cpp
  src/experiments/history_probe.cpp
  src/experiments/tab_actions_probe.cpp
  src/experiments/audible_probe.cpp
  src/experiments/downloads_page_probe.cpp
  src/experiments/extension_popup_probe.cpp
  src/experiments/protocols_probe.cpp
  src/experiments/close_motion_probe.cpp
  src/experiments/sidebar_peek_probe.cpp
  src/experiments/sidebar_perf_probe.cpp
  src/experiments/pip_probe.cpp
  src/experiments/capture_mute_probe.cpp
  src/experiments/theme_probe.cpp
  src/experiments/spaces_probe.cpp
  src/experiments/boost_probe.cpp
  src/experiments/menu_probe.cpp
  src/experiments/private_probe.cpp
  src/experiments/pin_probe.cpp
  src/experiments/ua_brand_probe.cpp
  src/experiments/link_headers_probe.cpp src/experiments/page_background_probe.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/window_close.cpp)
# Window/taskbar icon PNGs generated from icons/logo_sol.svg (scripts/make_icons.py).
target_sources(${CEF_TARGET} PRIVATE resources/red.rc)
# The resource compiler does not track RCDATA files: rebuild red.rc when they change.
set_source_files_properties(resources/red.rc PROPERTIES OBJECT_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/red-32.png;${CMAKE_CURRENT_SOURCE_DIR}/resources/red-48.png;${CMAKE_CURRENT_SOURCE_DIR}/resources/red-64.png")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(src/app/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/red_version.h" @ONLY)
configure_file(resources/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc" @ONLY)
target_sources(${CEF_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc")
target_include_directories(${CEF_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_sources(${CEF_TARGET} PRIVATE src/windows/command_bar.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/split_view.cpp)
target_sources(${CEF_TARGET} PRIVATE src/workspace/split_controller.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/settings_window.cpp src/windows/default_browser.cpp src/navigation/external_launch.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/sidebar_menu.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/devtools.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/devtools_dock.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/device_emulation.cpp)
target_sources(${CEF_TARGET} PRIVATE src/import/zen_reader.cpp src/import/zen_import.cpp src/import/browser_import.cpp src/import/cookie_writer.cpp src/import/password_sink.cpp)
target_link_libraries(${CEF_TARGET} PRIVATE comctl32 ole32 uuid)
target_sources(${CEF_TARGET} PRIVATE src/experiments/lifecycle_probe.cpp src/experiments/toolbar_probe.cpp)
target_sources(${CEF_TARGET} PRIVATE src/extensions/profile_guard.cpp src/extensions/install_notice.cpp src/extensions/pinned_extensions.cpp src/extensions/extension_catalog.cpp src/windows/extension_popup.cpp src/windows/sidebar_peek.cpp
  src/windows/extensions_window.cpp src/site/site_data.cpp)
target_sources(${CEF_TARGET} PRIVATE src/downloads/download_manager.cpp src/windows/panel_popup.cpp src/windows/share_sheet.cpp src/windows/window_icons.cpp)
target_link_libraries(${CEF_TARGET} PRIVATE runtimeobject)
target_include_directories(${CEF_TARGET} PRIVATE src)
SET_LIBRARY_TARGET_PROPERTIES(${CEF_TARGET})
target_compile_options(${CEF_TARGET} PRIVATE /W4 /utf-8)
target_sources(${CEF_TARGET} PRIVATE src/windows/page_transition.cpp src/windows/page_presentation.cpp)
target_sources(${CEF_TARGET} PRIVATE src/windows/page_link_status.cpp src/windows/link_status.cpp)
target_sources(${CEF_TARGET} PRIVATE src/experiments/page_load_trace.cpp)
target_sources(${CEF_TARGET} PRIVATE src/experiments/platform_codecs_probe.cpp)
target_sources(${CEF_TARGET} PRIVATE src/experiments/devtools_probe.cpp)
target_link_libraries(${CEF_TARGET} PRIVATE workspace_store import_formats libcef_dll_wrapper libcef_lib ${CEF_STANDARD_LIBS} advapi32 dwmapi dcomp d3d11 d2d1 dwrite dxgi dxguid windowscodecs)
COPY_SINGLE_FILE(${CEF_TARGET} "${CEF_BINARY_DIR}/bootstrap.exe" "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.exe")
# Every Red process (renderers included) starts from this executable, and its
# main thread gets the stack the image asks for. CEF's bootstrap asks for 1 MB;
# chrome.exe and msedge.exe ask for 8 MB because Blink recurses deeply on deep
# DOM trees. With 1 MB a renderer overflowed its stack on a page nested 400
# levels deep, and the DevTools element picker over heavy pages crashed the
# frontend renderer (docs/devtools.md).
get_filename_component(RED_LINKER_DIR "${CMAKE_LINKER}" DIRECTORY)
find_program(RED_EDITBIN editbin HINTS "${RED_LINKER_DIR}" REQUIRED)
add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
  COMMAND "${RED_EDITBIN}" /NOLOGO /STACK:0x800000 "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.exe"
  VERBATIM)
COPY_FILES(${CEF_TARGET} "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES(${CEF_TARGET} "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
add_custom_target(browser_ui_assets ALL
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${CMAKE_SOURCE_DIR}/apps/browser-ui/dist" "${CEF_TARGET_OUT_DIR}/ui"
  COMMENT "Staging built browser UI"
  VERBATIM)
add_dependencies(${CEF_TARGET} browser_ui_assets)
SET_LPAC_ACLS(${CEF_TARGET})

add_executable(trust_policy_test "${CMAKE_SOURCE_DIR}/tests/native/trust_policy_test.cpp")
target_include_directories(trust_policy_test PRIVATE src)
target_compile_options(trust_policy_test PRIVATE /W4 /WX)
add_test(NAME trust_policy COMMAND trust_policy_test)

add_executable(profile_location_test "${CMAKE_SOURCE_DIR}/tests/native/profile_location_test.cpp")
target_include_directories(profile_location_test PRIVATE src)
target_compile_options(profile_location_test PRIVATE /W4 /WX /EHsc)
add_test(NAME profile_location COMMAND profile_location_test)

add_executable(boost_store_test "${CMAKE_SOURCE_DIR}/tests/native/boost_store_test.cpp" src/site/boost_store.cpp)
target_include_directories(boost_store_test PRIVATE src)
target_compile_options(boost_store_test PRIVATE /W4 /WX)
add_test(NAME boost_store COMMAND boost_store_test)

add_executable(favicon_cache_test "${CMAKE_SOURCE_DIR}/tests/native/favicon_cache_test.cpp")
target_include_directories(favicon_cache_test PRIVATE src)
target_compile_options(favicon_cache_test PRIVATE /W4 /WX)
add_test(NAME favicon_cache COMMAND favicon_cache_test)
add_executable(device_model_test "${CMAKE_SOURCE_DIR}/tests/native/device_model_test.cpp")
target_include_directories(device_model_test PRIVATE src)
target_compile_options(device_model_test PRIVATE /W4 /WX)
add_test(NAME device_model COMMAND device_model_test)
add_executable(menu_placement_test "${CMAKE_SOURCE_DIR}/tests/native/menu_placement_test.cpp")
target_include_directories(menu_placement_test PRIVATE src)
target_compile_options(menu_placement_test PRIVATE /W4 /WX)
add_test(NAME menu_placement COMMAND menu_placement_test)

add_executable(workspace_store_test "${CMAKE_SOURCE_DIR}/tests/native/workspace_store_test.cpp")
target_link_libraries(workspace_store_test PRIVATE workspace_store)
target_compile_options(workspace_store_test PRIVATE /W4 /WX /utf-8)
set_property(TARGET workspace_store_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME workspace_store COMMAND workspace_store_test)
add_executable(folder_removal_test "${CMAKE_SOURCE_DIR}/tests/native/folder_removal_test.cpp")
target_link_libraries(folder_removal_test PRIVATE workspace_store)
target_compile_options(folder_removal_test PRIVATE /W4 /WX /utf-8)
set_property(TARGET folder_removal_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME folder_removal COMMAND folder_removal_test)
add_executable(history_store_test "${CMAKE_SOURCE_DIR}/tests/native/history_store_test.cpp")
target_link_libraries(history_store_test PRIVATE workspace_store)
target_compile_options(history_store_test PRIVATE /W4 /WX /utf-8)
set_property(TARGET history_store_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME history_store COMMAND history_store_test)
add_executable(browser_import_test "${CMAKE_SOURCE_DIR}/tests/native/browser_import_test.cpp")
target_link_libraries(browser_import_test PRIVATE import_formats)
target_compile_options(browser_import_test PRIVATE /W4 /WX /utf-8)
target_compile_definitions(browser_import_test PRIVATE "RED_IMPORT_FIXTURES=L\"${CMAKE_SOURCE_DIR}/tests/fixtures/import\"")
set_property(TARGET browser_import_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME browser_import COMMAND browser_import_test)
add_executable(navigation_test "${CMAKE_SOURCE_DIR}/tests/native/navigation_test.cpp")
target_include_directories(navigation_test PRIVATE src)
target_compile_options(navigation_test PRIVATE /W4 /WX /utf-8)
target_compile_definitions(navigation_test PRIVATE _HAS_EXCEPTIONS=0 NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME navigation COMMAND navigation_test)
add_executable(close_sequence_test "${CMAKE_SOURCE_DIR}/tests/native/close_sequence_test.cpp")
target_include_directories(close_sequence_test PRIVATE src)
target_compile_options(close_sequence_test PRIVATE /W4 /WX /utf-8)
target_compile_definitions(close_sequence_test PRIVATE _HAS_EXCEPTIONS=0 NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME close_sequence COMMAND close_sequence_test)
add_executable(download_names_test "${CMAKE_SOURCE_DIR}/tests/native/download_names_test.cpp")
target_include_directories(download_names_test PRIVATE src)
target_compile_options(download_names_test PRIVATE /W4 /WX /utf-8)
target_compile_definitions(download_names_test PRIVATE _HAS_EXCEPTIONS=0 NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME download_names COMMAND download_names_test)
add_executable(sidebar_menu_test "${CMAKE_SOURCE_DIR}/tests/native/sidebar_menu_test.cpp")
target_include_directories(sidebar_menu_test PRIVATE src)
target_compile_options(sidebar_menu_test PRIVATE /W4 /WX /utf-8)
add_test(NAME sidebar_menu COMMAND sidebar_menu_test)
add_executable(extension_manifest_test "${CMAKE_SOURCE_DIR}/tests/native/extension_manifest_test.cpp")
target_include_directories(extension_manifest_test PRIVATE src)
target_compile_options(extension_manifest_test PRIVATE /W4 /WX /EHsc)
add_test(NAME extension_manifest COMMAND extension_manifest_test)
add_executable(site_data_test "${CMAKE_SOURCE_DIR}/tests/native/site_data_test.cpp")
target_include_directories(site_data_test PRIVATE src)
target_compile_options(site_data_test PRIVATE /W4 /WX /EHsc)
add_test(NAME site_data COMMAND site_data_test)
add_executable(theme_test "${CMAKE_SOURCE_DIR}/tests/native/theme_test.cpp" src/app/theme.cpp)
target_include_directories(theme_test PRIVATE src)
target_compile_options(theme_test PRIVATE /W4 /WX /EHsc)
target_link_libraries(theme_test PRIVATE advapi32)
add_test(NAME theme COMMAND theme_test)
add_executable(link_status_text_test "${CMAKE_SOURCE_DIR}/tests/native/link_status_text_test.cpp")
target_include_directories(link_status_text_test PRIVATE src)
target_compile_options(link_status_text_test PRIVATE /W4 /WX /utf-8 /EHsc)
add_test(NAME link_status_text COMMAND link_status_text_test)
