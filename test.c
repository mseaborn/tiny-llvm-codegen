
int test_return(int arg) {
  return 123;
}

int test_add(int arg) {
  return arg + 100;
}

int test_sub(int arg) {
  return 1000 - arg;
}

int test_load_int32(int *ptr) {
  return *ptr;
}

void test_store_int32(int *ptr, int value) {
  *ptr = value;
}
