#!/bin/bash
# Build script for MPI Usage Sanitizer
# Usage: ./build.sh

set -e

echo "=== MPI Usage Sanitizer — Build ==="
echo ""

# Check dependencies
echo "[1/4] Checking dependencies..."
for cmd in clang-17 cmake make mpicc; do
    if ! command -v $cmd &> /dev/null; then
        echo "ERROR: '$cmd' not found. Install with:"
        echo "  sudo apt install clang-17 llvm-17-dev cmake make openmpi-bin libopenmpi-dev"
        exit 1
    fi
done
echo "  All dependencies found."

# Build pass and runtime
echo "[2/4] Creating build directory..."
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$SCRIPT_DIR/build"
cd "$SCRIPT_DIR/build"

echo "[3/4] Running CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -1

echo "[4/4] Compiling..."
make -j$(nproc) 2>&1 | tail -3

# Verify outputs
echo ""
if [ -f "$SCRIPT_DIR/build/MPISanPass.so" ] && [ -f "$SCRIPT_DIR/build/libmpiasan_rt.so" ]; then
    echo "=== BUILD SUCCESSFUL ==="
    echo "  Pass:    build/MPISanPass.so"
    echo "  Runtime: build/libmpiasan_rt.so"
    echo "  Wrapper: mpiasan-cc"
    echo ""
    echo "Run ./run.sh to execute all tests."
else
    echo "=== BUILD FAILED ==="
    exit 1
fi
