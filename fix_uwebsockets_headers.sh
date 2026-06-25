#!/bin/bash

# Quick fix script to copy uSockets headers to the correct location
# Run this from the project root directory

set -e

echo "Fixing uWebSockets header paths..."

# Check if uWebSockets exists
if [ ! -d "third_party/uWebSockets" ]; then
    echo "Error: third_party/uWebSockets not found!"
    echo "Please run: cd third_party && git clone https://github.com/uNetworking/uWebSockets.git"
    exit 1
fi

# Initialize uSockets submodule if needed
cd third_party/uWebSockets
if [ ! -d "uSockets/src" ]; then
    echo "Initializing uSockets submodule..."
    git submodule update --init --recursive
fi

# Copy uSockets headers to main include directory
echo "Copying uSockets headers..."
mkdir -p ../include
cp uSockets/src/*.h ../include/

# Copy uWebSockets headers
echo "Copying uWebSockets headers..."
mkdir -p ../include/uWebSockets
cp src/*.h ../include/uWebSockets/

# Build uSockets library if needed
if [ ! -f "../lib/libuSockets.a" ]; then
    echo "Building uSockets library..."
    cd uSockets
    WITH_OPENSSL=1 make -j$(nproc) CFLAGS="-O3 -fPIC -fno-lto" CXXFLAGS="-fno-lto" LDFLAGS="-fno-lto"
    mkdir -p ../../lib
    cp uSockets.a ../../lib/libuSockets.a
    cd ..
fi

cd ../..

echo ""
echo "✓ Headers fixed successfully!"
echo ""
echo "Verification:"
ls -lh third_party/include/libusockets.h 2>/dev/null && echo "  ✓ libusockets.h found" || echo "  ✗ libusockets.h NOT found"
ls -lh third_party/include/uWebSockets/App.h 2>/dev/null && echo "  ✓ App.h found" || echo "  ✗ App.h NOT found"
ls -lh third_party/lib/libuSockets.a 2>/dev/null && echo "  ✓ libuSockets.a found" || echo "  ✗ libuSockets.a NOT found"
echo ""
echo "You can now build with: cd build && make shardora -j\$(nproc)"
