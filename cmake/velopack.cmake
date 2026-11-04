file(READ "${CMAKE_SOURCE_DIR}/cmake/velopack.lock.json" VELOPACK_LOCK)
string(JSON VELOPACK_EXPECTED_HASH GET "${VELOPACK_LOCK}" sha256)
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/velopack/.archive-sha256")
  message(FATAL_ERROR "Run python scripts/fetch_velopack.py")
endif()
file(READ "${CMAKE_SOURCE_DIR}/third_party/velopack/.archive-sha256" VELOPACK_EXTRACTED_HASH)
string(STRIP "${VELOPACK_EXTRACTED_HASH}" VELOPACK_EXTRACTED_HASH)
if(NOT VELOPACK_EXTRACTED_HASH STREQUAL VELOPACK_EXPECTED_HASH)
  message(FATAL_ERROR "Velopack lock mismatch")
endif()
add_library(velopack SHARED IMPORTED)
set_target_properties(velopack PROPERTIES
  IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_party/velopack/velopack_libc_win_x64_msvc.dll"
  IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/third_party/velopack/velopack_libc_win_x64_msvc.dll.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/velopack")
