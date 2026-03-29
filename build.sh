#!/bin/bash
# build.sh - Clean build + run for macOS LTE Uplink Monitor

set -e  # stop on any error

cd "$(dirname "$0")"

echo "=== Cleaning previous build ==="
rm -rf build

echo "=== Configuring with CMake ==="
mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release

echo "=== Building ==="
make -j$(sysctl -n hw.logicalcpu)

echo "=== Build completed successfully! ==="

echo ""
echo "Running the program..."
echo "Press Ctrl+C to stop."
echo ""

# Run the binary
./bin/lte_uplink_monitor
