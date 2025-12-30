cmake_minimum_required(VERSION 3.14)
project(PicConvertor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

unset(STB_IMAGE_FILE CACHE)

# Prefer local third_party/stb_image.h; fall back to downloading to build dir
# Search both the source third_party and build dir (in case it was downloaded previously)
find_file(STB_IMAGE_FILE stb_image.h PATHS ${CMAKE_SOURCE_DIR}/third_party ${CMAKE_BINARY_DIR})
if (STB_IMAGE_FILE)
  # Use the directory of the found file so we always set STB_IMAGE_DIR correctly
  get_filename_component(STB_IMAGE_DIR ${STB_IMAGE_FILE} DIRECTORY)
  message(STATUS "Found stb_image.h in ${STB_IMAGE_DIR}")
else()
  message(STATUS "Local stb_image.h not found, attempting to download into build dir")
  set(STB_URL "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h")
  set(STB_LOCAL "${CMAKE_BINARY_DIR}/stb_image.h")

  # First attempt (normal TLS verification)
  file(DOWNLOAD ${STB_URL} ${STB_LOCAL} STATUS _stb_status LOG _stb_log)
  list(GET _stb_status 0 _stb_code)
  if (NOT _stb_code EQUAL 0)
    message(WARNING "Initial download of stb_image.h failed; retrying without TLS certificate verification. First download log:\n${_stb_log}")

    # Retry with TLS verification disabled (less secure fallback)
    file(DOWNLOAD ${STB_URL} ${STB_LOCAL} TLS_VERIFY FALSE STATUS _stb_status2 LOG _stb_log2)
    list(GET _stb_status2 0 _stb_code2)

    if (NOT _stb_code2 EQUAL 0 OR NOT EXISTS ${STB_LOCAL})
      message(FATAL_ERROR "Failed to download stb_image.h from ${STB_URL} (attempted normal download and retry with TLS_VERIFY=FALSE).\nPlease manually download it into ${CMAKE_SOURCE_DIR}/third_party/stb_image.h from:\n${STB_URL}\n\nFirst attempt log:\n${_stb_log}\n\nSecond attempt log:\n${_stb_log2}")
    else()
      set(STB_IMAGE_DIR ${CMAKE_BINARY_DIR})
      message(STATUS "Downloaded stb_image.h into ${STB_LOCAL} (download succeeded with TLS_VERIFY=FALSE)")
    endif()

  else()
    # initial download succeeded — double-check the file exists before declaring success
    if (EXISTS ${STB_LOCAL})
      set(STB_IMAGE_DIR ${CMAKE_BINARY_DIR})
      message(STATUS "Downloaded stb_image.h into ${STB_LOCAL}")
    else()
      message(FATAL_ERROR "Download reported success but ${STB_LOCAL} does not exist; please download stb_image.h manually from ${STB_URL} into ${CMAKE_SOURCE_DIR}/third_party/stb_image.h")
    endif()
  endif()
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
message(STATUS "Config: STB_IMAGE from ${STB_IMAGE_DIR}")
