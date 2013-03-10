
#ifndef ARITHMETIC_TEST_H_
#define ARITHMETIC_TEST_H_ 1

struct TestFunc {
  const char *name;
  void (*func)(void *arg1, void *arg2, void *result);
};

extern struct TestFunc test_funcs_c[];
extern struct TestFunc test_funcs_ll[];

#endif
