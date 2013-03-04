#!/bin/bash

set -eu

llvm_config=llvm-config-3.1

# Filter out -O2 to reduce compile time
cflags="$(
  for flag in $($llvm_config --cxxflags); do
    case "$flag" in
      -O*) ;;
      *) echo $flag;;
    esac
  done)"
cflags="$cflags -UNDEBUG -Wall -Werror"

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
  $($llvm_config --ldflags --libs) -ldl \
  -o codegen_test

g++ -m32 \
  expand_getelementptr.o \
  codegen.o \
  run_program.o \
  $($llvm_config --ldflags --libs) -ldl \
  -o run_program

clang -m32 -O2 -c -emit-llvm hellow_minimal_irt.c -o hellow_minimal_irt.pexe

./codegen_test

./run_program hellow_minimal_irt.pexe
