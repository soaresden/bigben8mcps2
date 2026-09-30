#!/bin/sh
# XMRAW.IRX: raw memory card page reader used by XMEM2 (read-only)
set -e
cd "$(dirname "$0")"; . ../env.sh
mkdir -p build
build_irx build/xmraw.irx xmraw.c imports.c
echo "built src/xmraw/build/xmraw.irx"
