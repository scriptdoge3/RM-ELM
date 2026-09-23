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
# The panels are plain 2D shapes, so default to an OpenGL 2.1 context: it opens
# on almost every driver, where a 3.3 core context is refused by some.
option(RMELM_WAYLAND "Build the GUI with native Wayland support when available" ON)
option(RMELM_GL33 "Build raylib for OpenGL 3.3 core instead of 2.1" OFF)

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
  src/core/rm_game.c
  src/core/rm_rps.c
  src/core/rm_tg.c
  src/core/rm_aux.c
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
    # Native Wayland: without it GLFW runs through XWayland's GLX, which some
    # drivers (notably NVIDIA on Wayland) refuse. GLFW picks Wayland at run
    # time when WAYLAND_DISPLAY is set and falls back to X11 otherwise.
    if(RMELM_WAYLAND AND UNIX AND NOT APPLE)
      find_package(PkgConfig QUIET)
      find_program(RMELM_WAYLAND_SCANNER wayland-scanner)
      if(PKG_CONFIG_FOUND AND RMELM_WAYLAND_SCANNER)
        pkg_check_modules(RMELM_WL QUIET wayland-client wayland-cursor wayland-egl xkbcommon)
      endif()
      if(RMELM_WL_FOUND)
        set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
        message(STATUS "RM-ELM: GUI with native Wayland and X11")
      else()
        message(STATUS "RM-ELM: Wayland dev files not found, GUI is X11 only")
      endif()
    endif()
    if(RMELM_GL33)
      set(OPENGL_VERSION "3.3" CACHE STRING "" FORCE)
    else()
      set(OPENGL_VERSION "2.1" CACHE STRING "" FORCE)
    endif()
    FetchContent_Declare(raylib
      GIT_REPOSITORY https://github.com/raysan5/raylib.git
      GIT_TAG 5.5
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)
    # raylib's own sources are not ours to fix; keep the build log readable
    if(NOT MSVC)
      foreach(t raylib glfw)
        if(TARGET ${t})
          target_compile_options(${t} PRIVATE -w)
        endif()
      endforeach()
    endif()
  endif()
  find_package(Threads REQUIRED)
  # the panel lettering font, compiled into the program
  set(RMELM_FONT_TTF ${CMAKE_SOURCE_DIR}/assets/fonts/DejaVuSans-Bold.ttf)
  set(RMELM_FONT_C ${CMAKE_BINARY_DIR}/gui_font_data.c)
  add_custom_command(OUTPUT ${RMELM_FONT_C}
    COMMAND ${CMAKE_COMMAND} -DIN=${RMELM_FONT_TTF} -DOUT=${RMELM_FONT_C} -DNAME=gui_font_ttf
            -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${RMELM_FONT_TTF} ${CMAKE_SOURCE_DIR}/cmake/embed.cmake)
  add_executable(rmelm_gui src/gui/rmelm_gui.c src/gui/gui_widgets.c src/gui/gui_font.c src/gui/gui_ann.c
    src/gui/gui_board.c src/gui/gui_reactor.c src/gui/gui_hts.c src/gui/gui_tg.c src/gui/gui_elec.c src/gui/gui_aux.c
    src/gui/gui_rsp.c ${RMELM_FONT_C})
  target_link_libraries(rmelm_gui PRIVATE rmelm_core raylib Threads::Threads)
  if(NOT MSVC)
    # raylib's headers are not strict-C17 clean
    target_compile_options(rmelm_gui PRIVATE -Wno-shadow)
  endif()
endif()

if(RMELM_BUILD_TESTS)
  enable_testing()
  foreach(t test_props test_kernels test_diffusion test_core test_plant test_startup test_swr test_game test_systems)
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} PRIVATE rmelm_core)
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()

message(STATUS "RM-ELM: assembly kernels ${RMELM_ASM_ACTIVE}")
