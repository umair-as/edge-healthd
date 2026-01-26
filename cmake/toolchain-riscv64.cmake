# CMake toolchain file for cross-compiling to riscv64 (VisionFive2)
# Use with Yocto SDK or standalone cross-compiler

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

# Toolchain prefix (adjust based on your SDK)
set(CROSS_COMPILE "riscv64-poky-linux-" CACHE STRING "Cross-compiler prefix")

# If using Yocto SDK, these are typically set by environment-setup script
# Otherwise, specify paths explicitly:
#
# set(CMAKE_C_COMPILER   /opt/poky/sysroots/x86_64-pokysdk-linux/usr/bin/riscv64-poky-linux/riscv64-poky-linux-gcc)
# set(CMAKE_CXX_COMPILER /opt/poky/sysroots/x86_64-pokysdk-linux/usr/bin/riscv64-poky-linux/riscv64-poky-linux-g++)
# set(CMAKE_SYSROOT      /opt/poky/sysroots/riscv64-poky-linux)

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Compiler flags for riscv64 (VisionFive2 uses rv64gc)
set(CMAKE_C_FLAGS_INIT   "-march=rv64gc -mabi=lp64d")
set(CMAKE_CXX_FLAGS_INIT "-march=rv64gc -mabi=lp64d")
