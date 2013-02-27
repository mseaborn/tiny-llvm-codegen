#!/bin/bash

set -eu

# Filter out -O2 to reduce compile time
cflags="$(
  for flag in $(llvm-config-3.0 --cxxflags); do
    case "$flag" in
      -O*) ;;
      *) echo $flag;;
    esac
  done)"
cflags="$cflags -UNDEBUG"

python test_generate_code.py > gen_arithmetic_test.c
gcc -O1 -m32 -c gen_arithmetic_test.c
clang -O1 -m32 -c gen_arithmetic_test.c -emit-llvm -o gen_arithmetic_test.ll

g++ -m32 $cflags -c codegen.cc
g++ -m32 $cflags -c codegen_test.cc
g++ -m32 codegen.o codegen_test.o gen_arithmetic_test.o \
  $(llvm-config-3.0 --ldflags --libs) -ldl \
  -Wall -o codegen_test

./codegen_test
