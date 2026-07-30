cmake_minimum_required(VERSION 3.20)

project(doodad_native_host LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(DOODAD_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(WAMR_ROOT_DIR
    "${DOODAD_ROOT}/firmware/managed_components/espressif__wasm-micro-runtime")
set(LVGL_ROOT_DIR
    "${DOODAD_ROOT}/firmware/managed_components/lvgl__lvgl")

if(NOT EXISTS "${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")
  message(FATAL_ERROR
    "Pinned WAMR source is missing. Run idf.py -C firmware reconfigure first.")
endif()
if(NOT EXISTS "${LVGL_ROOT_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "Pinned LVGL source is missing. Run idf.py -C firmware reconfigure first.")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH
    "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE FILEPATH "" FORCE)
add_subdirectory("${LVGL_ROOT_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
add_subdirectory(
  "${DOODAD_ROOT}/components/m3e_lvgl"
  "${CMAKE_CURRENT_BINARY_DIR}/m3e_lvgl")

set(WAMR_BUILD_PLATFORM "darwin")
set(WAMR_BUILD_TARGET "AARCH64")
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_SHARED_MEMORY 0)
set(WAMR_BUILD_BULK_MEMORY 1)
set(WAMR_BUILD_SHRUNK_MEMORY 1)
include("${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")

add_library(doodad_wamr STATIC ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(doodad_wamr PUBLIC
  "${WAMR_ROOT_DIR}/core/iwasm/include"
  "${WAMR_ROOT_DIR}/core/shared/include"
  "${WAMR_ROOT_DIR}/core/shared/platform/include")

add_library(doodad_native_host SHARED
  src/doodad_native_host.c
  "${DOODAD_ROOT}/ui/doodad_lvgl_ui.c")
target_include_directories(doodad_native_host PRIVATE
  include
  "${DOODAD_ROOT}/ui")
target_link_libraries(doodad_native_host PRIVATE
  doodad_wamr
  m3e::lvgl
  lvgl::lvgl)

include(CTest)
if(BUILD_TESTING)
  add_executable(m3e_display_profile_test
    "${DOODAD_ROOT}/tests/m3e/display_profile_test.cpp")
  target_link_libraries(m3e_display_profile_test PRIVATE m3e::lvgl)
  add_test(NAME m3e_display_profile COMMAND m3e_display_profile_test)

  add_executable(m3e_core_tokens_test
    "${DOODAD_ROOT}/tests/m3e/core_tokens_test.cpp")
  target_link_libraries(m3e_core_tokens_test PRIVATE m3e::lvgl)
  add_test(NAME m3e_core_tokens COMMAND m3e_core_tokens_test)

  add_executable(m3e_framework_test
    "${DOODAD_ROOT}/tests/m3e/framework_test.cpp")
  target_link_libraries(m3e_framework_test PRIVATE m3e::lvgl)
  add_test(NAME m3e_framework COMMAND m3e_framework_test)

  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(
    NAME m3e_token_sync
    COMMAND "${Python3_EXECUTABLE}"
      "${DOODAD_ROOT}/tools/token_sync/sync.py" --check)
endif()

if(APPLE)
  target_link_libraries(doodad_native_host PRIVATE pthread m)
endif()
