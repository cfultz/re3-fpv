cmake_minimum_required(VERSION 3.24)
project(re3_fpv LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(LIBFPV_DIR "$ENV{LIBFPV_DIR}" CACHE PATH "Path to libfpv repo")
if(NOT LIBFPV_DIR)
    set(LIBFPV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libfpv" CACHE PATH "Path to libfpv repo" FORCE)
endif()

include(FetchContent)
set(SAFETYHOOK_FETCH_ZYDIS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    safetyhook
    GIT_REPOSITORY https://github.com/cursey/safetyhook.git
    GIT_TAG v0.6.4
)
FetchContent_MakeAvailable(safetyhook)

add_library(fpv_re3 SHARED
    src/main.cpp
)

target_include_directories(fpv_re3 PRIVATE
    "${LIBFPV_DIR}/include"
    "${LIBFPV_DIR}/docs/renderers"
)

target_link_libraries(fpv_re3 PRIVATE
    safetyhook::safetyhook
    "${LIBFPV_DIR}/build/linux/libfpv.so"
    GL
    dl
)

set_target_properties(fpv_re3 PROPERTIES
    BUILD_RPATH "${LIBFPV_DIR}/build/linux"
)
