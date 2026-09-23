cmake_minimum_required(VERSION 3.16)
project(RMELM VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RMELM_USE_ASM "Use the hand-written x86-64 assembly kernels" ON)
option(RMELM_BUILD_TESTS "Build the test programs" ON)
option(RMELM_GUI "Build the graphical control room (raylib)" ON)
option(RMELM_GL21 "Build raylib for OpenGL 2.1 (older or virtual GPUs)" OFF)

# The assembly kernels follow the System V AMD64 ABI and are written in GAS
# syntax, so they need GCC or Clang on an x86-64 target. Everything else
# (ARM, MSVC) gets the portable C kernels.
set(RMELM_ASM_ACTIVE OFF)
if(RMELM_USE_ASM AND NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  enable_language(ASM)
  set(RMELM_ASM_ACTIVE ON)
endif()

if(MSVC)
  add_compile_options(/W4 /fp:precise)
else()
  # -ffp-contract=off keeps the C reference kernels bit-for-bit comparable
  # with the assembly ones (no silent FMA fusion).
  add_compile_options(-Wall -Wextra -Wshadow -ffp-contract=off)
endif()

set(RMELM_CORE_SOURCES
  src/core/rm_if97.c
  src/core/rm_sodium.c
  src/core/rm_materials.c
  src/core/rm_kernels.c
  src/core/rm_hexgrid.c
  src/core/rm_diffusion.c
  src/core/rm_coreth.c
  src/core/rm_xs.c
  src/core/rm_xsdata.c
  src/core/rm_core.c
  src/core/rm_plant.c
)

if(RMELM_ASM_ACTIVE)
  list(APPEND RMELM_CORE_SOURCES
    src/asm/rm_pk_x86_64.S
    src/asm/rm_tridiag_x86_64.S
  )
endif()

add_library(rmelm_core STATIC ${RMELM_CORE_SOURCES})
target_include_directories(rmelm_core PUBLIC src/core)
if(RMELM_ASM_ACTIVE)
  target_compile_definitions(rmelm_core PUBLIC RMELM_HAVE_ASM=1)
endif()
if(NOT MSVC)
  target_link_libraries(rmelm_core PUBLIC m)
endif()

if(NOT WIN32)
  add_executable(rmelm src/ppc/rmelm.c)
  target_link_libraries(rmelm PRIVATE rmelm_core)
endif()

if(RMELM_GUI)
  find_package(raylib 5.0 QUIET)
  if(NOT raylib_FOUND)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    if(RMELM_GL21)
      set(OPENGL_VERSION "2.1" CACHE STRING "" FORCE)
    endif()
    FetchContent_Declare(raylib
      GIT_REPOSITORY https://github.com/raysan5/raylib.git
      GIT_TAG 5.5
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)
  endif()
  find_package(Threads REQUIRED)
  add_executable(rmelm_gui src/gui/rmelm_gui.c)
  target_link_libraries(rmelm_gui PRIVATE rmelm_core raylib Threads::Threads)
  if(NOT MSVC)
    # raylib's headers are not strict-C17 clean
    target_compile_options(rmelm_gui PRIVATE -Wno-shadow)
  endif()
endif()

if(RMELM_BUILD_TESTS)
  enable_testing()
  foreach(t test_props test_kernels test_diffusion test_core test_plant)
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} PRIVATE rmelm_core)
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()

message(STATUS "RM-ELM: assembly kernels ${RMELM_ASM_ACTIVE}")
