#!/bin/sh

# vcpkg arm64
./vcpkg/vcpkg install --triplet=arm64-osx --x-install-root vcpkg_installed

# vcpkg x64 
./vcpkg/vcpkg install --triplet=x64-osx --x-install-root vcpkg_x86

# remove old universal libs 
rm -rf cache/univ-osx

# lipo into univ-osx, this is what will be used for linking
python3 lipo_dir_merge.py vcpkg_installed/arm64-osx vcpkg_x86/x64-osx cache/univ-osx
