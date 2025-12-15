#!/usr/bin/env bash

set -xe
mkdir -p build
gcc "$@" -c -o build/murmur3.o src/murmur/murmur3.c
gcc "$@" -Wall -Wextra -o build/main build/murmur3.o src/*.c -llz4
