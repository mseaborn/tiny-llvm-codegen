#!/bin/bash

set -eu

python test_generate_code.py > gen_arithmetic_test.c
gcc -O1 -m32 -c gen_arithmetic_test.c
clang -O1 -m32 -c gen_arithmetic_test.c -emit-llvm -o gen_arithmetic_test.ll

g++ -m32 codegen.cc gen_arithmetic_test.o \
  $(llvm-config-3.0 --cxxflags) \
  $(llvm-config-3.0 --ldflags --libs) -ldl \
  -UNDEBUG \
  -Wall -o codegen

./codegen
