# Cloud-sync libraries (docs/sync.md): pinned by cmake/<name>.lock.json and
# extracted by scripts/fetch_sync_deps.py. Portable C, no platform APIs.
foreach(SYNC_LIB monocypher zstd)
  file(READ "${CMAKE_SOURCE_DIR}/cmake/${SYNC_LIB}.lock.json" SYNC_LOCK)
  string(JSON SYNC_EXPECTED_HASH GET "${SYNC_LOCK}" sha256)
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/${SYNC_LIB}/.archive-sha256")
    message(FATAL_ERROR "Run python scripts/fetch_sync_deps.py")
  endif()
  file(READ "${CMAKE_SOURCE_DIR}/third_party/${SYNC_LIB}/.archive-sha256" SYNC_EXTRACTED_HASH)
  string(STRIP "${SYNC_EXTRACTED_HASH}" SYNC_EXTRACTED_HASH)
  if(NOT SYNC_EXTRACTED_HASH STREQUAL SYNC_EXPECTED_HASH)
    message(FATAL_ERROR "${SYNC_LIB} lock mismatch; run python scripts/fetch_sync_deps.py")
  endif()
endforeach()

set(MONOCYPHER_DIR "${CMAKE_SOURCE_DIR}/third_party/monocypher")
add_library(monocypher STATIC "${MONOCYPHER_DIR}/src/monocypher.c")
target_include_directories(monocypher PUBLIC "${MONOCYPHER_DIR}/src")
set_property(TARGET monocypher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(ZSTD_DIR "${CMAKE_SOURCE_DIR}/third_party/zstd/lib")
file(GLOB ZSTD_SOURCES "${ZSTD_DIR}/common/*.c" "${ZSTD_DIR}/compress/*.c" "${ZSTD_DIR}/decompress/*.c")
add_library(zstd STATIC ${ZSTD_SOURCES})
target_include_directories(zstd PUBLIC "${ZSTD_DIR}")
# Single-threaded, no legacy formats, no assembly; the portable C decoder.
target_compile_definitions(zstd PRIVATE ZSTD_LEGACY_SUPPORT=0 ZSTD_DISABLE_ASM ZSTD_NO_TRACE _CRT_SECURE_NO_WARNINGS)
set_property(TARGET zstd PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
