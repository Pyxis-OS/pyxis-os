# Pyxis platform description, installed as SDK/share/cmake/Platform/Pyxis.cmake
# and found through pyxis.cmake. Executables are P1F files named .pxe; there
# are only static libraries. Pyxis is neither UNIX nor WIN32.

set(CMAKE_EXECUTABLE_SUFFIX ".pxe")
set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
set(CMAKE_FIND_LIBRARY_PREFIXES "lib")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)

# Prefixes searched under each CMAKE_FIND_ROOT_PATH entry: the sysroot keeps
# its files under /usr, development prefixes such as build/ports-dev/sdl2 at
# their top.
set(CMAKE_SYSTEM_PREFIX_PATH "/usr" "/")
