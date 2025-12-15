#!/usr/bin/env bash

set -xe
gcc "$@" -Wall -Wextra -o build/main src/*.c -llz4
