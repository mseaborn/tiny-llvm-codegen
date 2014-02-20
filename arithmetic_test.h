//===- arithmetic_test.h - Functions for testing arithmetic instructions---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef ARITHMETIC_TEST_H_
#define ARITHMETIC_TEST_H_ 1

struct TestFunc {
  const char *name;
  void (*func)(void *arg1, void *arg2, void *result);
};

extern struct TestFunc test_funcs_c[];
extern struct TestFunc test_funcs_ll[];

#endif
