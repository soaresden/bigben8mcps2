#!/bin/sh
set -e
cd "$(dirname "$0")"; . ../env.sh
mkdir -p build
build_irx build/megamem_secrpatch.irx secrpatch.c imports.c
echo "built src/secrpatch/build/megamem_secrpatch.irx"
