set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Buildroot toolchain paths
set(BR2_HOST "/home/arun/projects/pico_display/output/host")
set(BR2_SYSROOT "${BR2_HOST}/aarch64-buildroot-linux-gnu/sysroot")

set(CMAKE_C_COMPILER "${BR2_HOST}/bin/aarch64-linux-gcc")
set(CMAKE_CXX_COMPILER "${BR2_HOST}/bin/aarch64-linux-g++")
set(CMAKE_SYSROOT "${BR2_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${BR2_SYSROOT}")

# Search for programs only on the host
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Search for libraries and headers only in the sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Use the buildroot pkg-config wrapper
set(ENV{PKG_CONFIG} "${BR2_HOST}/bin/pkg-config")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} "${BR2_SYSROOT}/usr/lib/pkgconfig:${BR2_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${BR2_SYSROOT}")
