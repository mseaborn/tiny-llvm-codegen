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
cflags="$cflags -UNDEBUG -Wall"

python test_generate_code.py > gen_arithmetic_test.c
gcc -O1 -m32 -c gen_arithmetic_test.c
clang -O1 -m32 -c gen_arithmetic_test.c -emit-llvm -o gen_arithmetic_test.ll

g++ -m32 $cflags -c expand_getelementptr.cc
g++ -m32 $cflags -c codegen.cc
g++ -m32 $cflags -c codegen_test.cc
g++ -m32 $cflags -c run_program.cc

g++ -m32 \
  expand_getelementptr.o \
  codegen.o \
  codegen_test.o \
  gen_arithmetic_test.o \
  $(llvm-config-3.0 --ldflags --libs) -ldl \
  -o codegen_test

g++ -m32 \
  expand_getelementptr.o \
  codegen.o \
  run_program.o \
  $(llvm-config-3.0 --ldflags --libs) -ldl \
  -o run_program

./codegen_test
