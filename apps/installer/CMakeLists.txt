# Sol's small installer (docs/installer.md): Sol-Web-Installer.exe downloads the
# latest release's Velopack setup, verifies it and installs it. Self-contained
# (static CRT, system DLLs only), so it runs on a clean Windows.
set(SOL_INSTALLER_DEFINES
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  SOL_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} SOL_VERSION_MINOR=${PROJECT_VERSION_MINOR} SOL_VERSION_PATCH=${PROJECT_VERSION_PATCH}
  "SOL_INSTALLER_VERSION=L\"${PROJECT_VERSION}\"")
set(SOL_INSTALLER_OPTIONS /W4 /WX /EHsc /utf-8 /permissive- $<$<CONFIG:Release>:/O2 /Gy /DNDEBUG>)

add_library(sol_installer_feed STATIC src/feed.cpp)
target_include_directories(sol_installer_feed PUBLIC src)
target_compile_definitions(sol_installer_feed PRIVATE UNICODE _UNICODE)
target_compile_options(sol_installer_feed PRIVATE ${SOL_INSTALLER_OPTIONS})
set_property(TARGET sol_installer_feed PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(sol_installer WIN32 src/main.cpp src/net.cpp src/view.cpp resources/installer.rc resources/installer.manifest)
target_compile_definitions(sol_installer PRIVATE ${SOL_INSTALLER_DEFINES})
target_compile_options(sol_installer PRIVATE ${SOL_INSTALLER_OPTIONS})
target_link_libraries(sol_installer PRIVATE sol_installer_feed d2d1 dwrite dwmapi winhttp bcrypt shcore shell32 windowscodecs ole32 user32 gdi32)
target_link_options(sol_installer PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
set_target_properties(sol_installer PROPERTIES OUTPUT_NAME "Sol-Web-Installer" MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(installer_feed_test "${CMAKE_SOURCE_DIR}/tests/native/installer_feed_test.cpp")
target_link_libraries(installer_feed_test PRIVATE sol_installer_feed)
target_compile_options(installer_feed_test PRIVATE ${SOL_INSTALLER_OPTIONS})
set_property(TARGET installer_feed_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_test(NAME installer_feed COMMAND installer_feed_test)
