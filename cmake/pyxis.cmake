# CMake toolchain file for Pyxis, installed as SDK/share/pyxis.cmake:
#
#   cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/sdk/share/pyxis.cmake ...
#
# Paths resolve from this file, so the SDK moves as one directory. The Pyxis
# Clang driver supplies the sysroot's headers, startup code, linker script and
# libraries, writes P1F executables and rejects shared libraries and PIE; this
# file only selects it and describes the platform to CMake.

set(CMAKE_SYSTEM_NAME Pyxis)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

get_filename_component(PYXIS_SDK "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PYXIS_SYSROOT "${PYXIS_SDK}/sysroot")
# Platform/Pyxis.cmake describes executables and libraries.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# The compiler is an external, prebuilt dependency found on PATH, as in
# pyxis.mk. -DPYXIS_CROSS_COMPILE=PREFIX or the CROSS_COMPILE environment
# variable selects another prefix; try-compile projects inherit the choice.
if(NOT DEFINED PYXIS_CROSS_COMPILE)
  if(DEFINED ENV{CROSS_COMPILE})
    set(PYXIS_CROSS_COMPILE "$ENV{CROSS_COMPILE}")
  else()
    set(PYXIS_CROSS_COMPILE "x86_64-unknown-pyxis-")
  endif()
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PYXIS_CROSS_COMPILE)
find_program(PYXIS_CLANG "${PYXIS_CROSS_COMPILE}clang" REQUIRED NO_CMAKE_FIND_ROOT_PATH)
find_program(PYXIS_CLANGXX "${PYXIS_CROSS_COMPILE}clang++" REQUIRED NO_CMAKE_FIND_ROOT_PATH)
set(CMAKE_C_COMPILER "${PYXIS_CLANG}")
set(CMAKE_CXX_COMPILER "${PYXIS_CLANGXX}")
set(CMAKE_ASM_COMPILER "${PYXIS_CLANG}")
set(CMAKE_SYSROOT "${PYXIS_SYSROOT}")

# C is freestanding, as in pyxis.mk, so Clang never turns code into calls to
# library functions the source did not make. C++ is hosted: libc++ needs it.
# Flags set on the command line replace these, so include -ffreestanding there.
set(CMAKE_C_FLAGS_INIT "-ffreestanding")
# The SDK's C headers come before Clang's builtin ones, as in pyxis.mk: in
# freestanding mode Clang's own stdint.h would define the int_fast types
# differently from libc. C++ keeps the driver's order, which puts libc++ first.
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "${PYXIS_SYSROOT}/usr/include")

# Look for libraries, headers and packages in the sysroot, never programs.
# Add development prefixes, such as Pyxis's build/ports-dev/sdl2, to
# CMAKE_FIND_ROOT_PATH.
list(APPEND CMAKE_FIND_ROOT_PATH "${PYXIS_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
