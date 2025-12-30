cmake_minimum_required(VERSION 3.14)
project(PicConvertor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer local third_party/stb_image.h; fall back to downloading to build dir
find_file(STB_IMAGE_FILE stb_image.h PATHS ${CMAKE_SOURCE_DIR}/third_party)
if (STB_IMAGE_FILE)
  message(STATUS "Found local stb_image.h in ${CMAKE_SOURCE_DIR}/third_party")
  set(STB_IMAGE_DIR ${CMAKE_SOURCE_DIR}/third_party)
else()
  message(STATUS "Local stb_image.h not found, attempting to download into build dir")
  set(STB_URL "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h")
  set(STB_LOCAL "${CMAKE_BINARY_DIR}/stb_image.h")
  file(DOWNLOAD ${STB_URL} ${STB_LOCAL} STATUS _stb_status LOG _stb_log)
  list(GET _stb_status 0 _stb_code)
  if (NOT _stb_code EQUAL 0)
    message(FATAL_ERROR "Failed to download stb_image.h. Please add stb_image.h to ${CMAKE_SOURCE_DIR}/third_party or enable network. Download log:\n${_stb_log}")
  endif()
  set(STB_IMAGE_DIR ${CMAKE_BINARY_DIR})
endif()

add_executable(picconvertor
  src/main.cpp
  src/image.cpp
  src/resample.cpp
  src/renderer.cpp
  src/TaskSystem.cpp
  src/Logger.cpp
)

# prune_runner target removed (prune runner no longer built)

target_include_directories(picconvertor PRIVATE
  ${STB_IMAGE_DIR}
  ${CMAKE_SOURCE_DIR}/src
)

# Detect CPU features for optional SIMD optimizations (AVX2)
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_MAVX2)
if (COMPILER_SUPPORTS_MAVX2)
  message(STATUS "Compiler supports -mavx2; enabling AVX2 optimizations")
  target_compile_options(picconvertor PRIVATE -mavx2)
  target_compile_definitions(picconvertor PRIVATE PICCONV_USE_AVX2=1)
elseif(MSVC)
  check_cxx_compiler_flag("/arch:AVX2" COMPILER_SUPPORTS_MSVC_AVX2)
  if (COMPILER_SUPPORTS_MSVC_AVX2)
    message(STATUS "MSVC supports /arch:AVX2; enabling AVX2 optimizations")
    target_compile_options(picconvertor PRIVATE /arch:AVX2)
    target_compile_definitions(picconvertor PRIVATE PICCONV_USE_AVX2=1)
  endif()
endif()

# For packaging or running tests later
install(TARGETS picconvertor RUNTIME DESTINATION bin)
message(STATUS "Config: STB_IMAGE from ${stb_image_file_SOURCE_DIR}")
