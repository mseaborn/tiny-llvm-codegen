#!/bin/bash

set -eu

g++ -m32 codegen.cc \
  $(llvm-config-3.0 --cxxflags) \
  $(llvm-config-3.0 --ldflags --libs) -ldl \
  -UNDEBUG \
  -Wall -o codegen

clang -m32 -emit-llvm -O1 -c test.c -o test.ll

./codegen
