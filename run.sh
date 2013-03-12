#!/bin/bash

set -eu

# If ccache is available, use it to speed up rebuilds.
ccache=""
if which ccache >/dev/null 2>&1; then
  ccache=ccache
fi

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

python test_generate_code.py --c-file > gen_arithmetic_test_c.c
$ccache gcc -O1 -m32 -c gen_arithmetic_test_c.c
$ccache clang -O1 -m32 -c gen_arithmetic_test_c.c -emit-llvm \
  -o gen_arithmetic_test_c.ll

python test_generate_code.py --ll-file > gen_arithmetic_test_ll.ll
$ccache clang -O1 -m32 -c gen_arithmetic_test_ll.ll

python generate_helpers.py --ll-file > gen_runtime_helpers_atomic.ll
python generate_helpers.py --header-file > gen_runtime_helpers_atomic.h
clang -O2 -m32 -c gen_runtime_helpers_atomic.ll -o gen_runtime_helpers_atomic.o

$ccache g++ -m32 $cflags -c expand_constantexpr.cc
$ccache g++ -m32 $cflags -c expand_getelementptr.cc
$ccache g++ -m32 $cflags -c expand_varargs.cc
$ccache g++ -m32 $cflags -c codegen.cc
$ccache g++ -m32 $cflags -c codegen_test.cc
$ccache g++ -m32 $cflags -c run_program.cc
$ccache g++ -m32 $cflags -c -O2 runtime_helpers.c

lib="
  expand_constantexpr.o
  expand_getelementptr.o
  expand_varargs.o
  codegen.o
  gen_runtime_helpers_atomic.o
  runtime_helpers.o"

g++ -m32 $lib \
  codegen_test.o \
  gen_arithmetic_test_c.o \
  gen_arithmetic_test_ll.o \
  $($llvm_config --ldflags --libs) -ldl \
  -o codegen_test

g++ -m32 $lib \
  run_program.o \
  $($llvm_config --ldflags --libs) -ldl \
  -o run_program

$ccache clang -m32 -O2 -c -emit-llvm hellow_minimal_irt.c \
  -o hellow_minimal_irt.pexe

./codegen_test

./run_program hellow_minimal_irt.pexe
