#!/usr/bin/env bash
# Build LFT with GCC (more up-to-date C++23 constexpr support)

set -e

# Use gcc-15/g++-15 explicitly (Homebrew GCC 15.2.0)
export CC=gcc-15
export CXX=g++-15

# Clean and rebuild
rm -rf build
mkdir -p build
cd build

# Configure with gcc
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "Build complete! Executables:"
ls -lh lft main thread_poc 2>/dev/null || true
