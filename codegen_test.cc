
#include <stdio.h>
#include <sys/mman.h>

#include <llvm/LLVMContext.h>
#include <llvm/Support/IRReader.h>

#include "arithmetic_test.h"
#include "codegen.h"

void my_assert(int val1, int val2, const char *expr1, const char *expr2,
               const char *file, int line_number) {
  fprintf(stderr, "%i != %i (0x%x != 0x%x): %s != %s at %s:%i\n",
          val1, val2,
          val1, val2,
          expr1, expr2, file, line_number);
  abort();
}

#define ASSERT_EQ(val1, val2)                                           \
  do {                                                                  \
    int _val1 = (val1);                                                 \
    int _val2 = (val2);                                                 \
    if (_val1 != _val2)                                                 \
      my_assert(_val1, _val2, #val1, #val2, __FILE__, __LINE__);        \
  } while (0);

int sub_func(int x, int y) {
  printf("sub_func(%i, %i) called\n", x, y);
  return x - y;
}

#define GET_FUNC(func, name) \
    printf("testing %s\n", name); \
    func = (typeof(func)) (globals[name]); \
    assert(func);

// We use this to check whether a memory read is of the correct size.
// If it is too big, it will cross a page boundary and fault.
class PageBoundary {
 public:
  PageBoundary() {
    addr_ = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
                 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(addr_ != MAP_FAILED);
    // Change the second page to be PROT_NONE.  We use
    // mmap()+MAP_FIXED rather than mprotect() because NaCl does not
    // support mprotect() yet.
    void *addr2 = mmap(get_boundary(), page_size, PROT_NONE,
                       MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
    assert(addr2 == get_boundary());
  }

  ~PageBoundary() {
    int rc = munmap(addr_, page_size * 2);
    assert(rc == 0);
  }

  void *get_boundary() {
    return (char *) addr_ + page_size;
  }

 private:
  static const int page_size = 0x10000; // NaCl-compatible
  void *addr_;
};

void test_features() {
  llvm::SMDiagnostic err;
  llvm::LLVMContext &context = llvm::getGlobalContext();
  const char *filename = "test.ll";
  llvm::Module *module = llvm::ParseIRFile(filename, err, context);
  if (!module) {
    fprintf(stderr, "failed to read file: %s\n", filename);
    assert(0);
  }

  std::map<std::string,uintptr_t> globals;
  translate(module, &globals);

  int (*func)(int arg);

  GET_FUNC(func, "test_return");
  assert(func(0) == 123);

  GET_FUNC(func, "test_add");
  assert(func(99) == 199);

  GET_FUNC(func, "test_sub");
  assert(func(200) == 800);

  {
    uint32_t (*funcp)(int *ptr);
    GET_FUNC(funcp, "test_load_int32");
    int value = 0x12345678;
    int cell = value;
    assert(funcp(&cell) == value);
  }

  {
    uint16_t (*funcp)(uint16_t *ptr);
    GET_FUNC(funcp, "test_load_int16");
    PageBoundary alloc;
    uint16_t *ptr = ((uint16_t *) alloc.get_boundary()) - 1;
    *ptr = 0x1234;
    ASSERT_EQ(funcp(ptr), 0x1234);
  }

  {
    uint8_t (*funcp)(uint8_t *ptr);
    GET_FUNC(funcp, "test_load_int8");
    PageBoundary alloc;
    uint8_t *ptr = ((uint8_t *) alloc.get_boundary()) - 1;
    *ptr = 0x12;
    ASSERT_EQ(funcp(ptr), 0x12);
  }

  {
    void (*funcp)(int *ptr, uint32_t value);
    GET_FUNC(funcp, "test_store_int32");
    int value = 0x12345678;
    int cell = 0;
    funcp(&cell, value);
    assert(cell == value);
  }

  {
    void (*funcp)(void *ptr, uint16_t value);
    GET_FUNC(funcp, "test_store_int16");
    char mem[] = { 1, 2, 3, 4 };
    int value = 0x1234;
    funcp(mem, value);
    assert(*(uint16_t *) mem == value);
    assert(mem[2] == 3);
    assert(mem[3] == 4);
  }

  {
    void (*funcp)(void *ptr, uint8_t value);
    GET_FUNC(funcp, "test_store_int8");
    char mem[] = { 1, 2, 3, 4 };
    int value = 0x12;
    funcp(mem, value);
    assert(*(uint8_t *) mem == value);
    assert(mem[1] == 2);
    assert(mem[2] == 3);
    assert(mem[3] == 4);
  }

  GET_FUNC(func, "test_compare");
  ASSERT_EQ(func(99), 1);
  ASSERT_EQ(func(98), 0);
  ASSERT_EQ(func(100), 0);

  GET_FUNC(func, "test_branch");
  ASSERT_EQ(func(0), 101);

  GET_FUNC(func, "test_conditional");
  ASSERT_EQ(func(99), 123);
  ASSERT_EQ(func(98), 456);

  GET_FUNC(func, "test_phi");
  ASSERT_EQ(func(99), 123);
  ASSERT_EQ(func(98), 456);

  {
    int (*funcp)(int (*func)(int arg1, int arg2), int arg1, int arg2);
    GET_FUNC(funcp, "test_call");
    ASSERT_EQ(funcp(sub_func, 50, 10), 1040);

    GET_FUNC(funcp, "test_call2");
    ASSERT_EQ(funcp(sub_func, 50, 10), 40);
  }

  {
    int (*funcp)(void);
    GET_FUNC(funcp, "test_direct_call");
    ASSERT_EQ(funcp(), 123);
  }

  {
    int *(*funcp)(void);
    GET_FUNC(funcp, "get_global");
    int *ptr = funcp();
    assert(ptr);
    ASSERT_EQ(*ptr, 124);
  }

  {
    char *(*funcp)(void);
    GET_FUNC(funcp, "get_global_string");
    char *str = funcp();
    assert(str);
    ASSERT_EQ(strcmp(str, "Hello!"), 0);
  }

  {
    short *(*funcp)(void);
    GET_FUNC(funcp, "get_global_array");
    short *array = funcp();
    ASSERT_EQ(array[0], 6);
    ASSERT_EQ(array[-1], 5);
  }

  {
    int **ptr_reloc = (int **) globals["ptr_reloc"];
    assert(*ptr_reloc == (int *) globals["global1"]);
  }

  {
    struct MyStruct { uint8_t a; uint32_t b; uint8_t c; };
    struct MyStruct *ptr = (struct MyStruct *) globals["struct_val"];
    ASSERT_EQ(ptr->a, 11);
    ASSERT_EQ(ptr->b, 22);
    ASSERT_EQ(ptr->c, 33);
    ptr = (struct MyStruct *) globals["struct_zero_init"];
    ASSERT_EQ(ptr->a, 0);
    ASSERT_EQ(ptr->b, 0);
    ASSERT_EQ(ptr->c, 0);
  }

  {
    int (*funcp)(void);
    GET_FUNC(funcp, "test_alloca");
    ASSERT_EQ(funcp(), 125);

    GET_FUNC(funcp, "test_alloca2");
    ASSERT_EQ(funcp(), 125);
  }

  {
    int *(*funcp)(char *arg);
    GET_FUNC(funcp, "test_bitcast");
    assert(funcp((char *) 0x12345678) == (int *) 0x12345678);
  }

  {
    uint32_t (*funcp)(uint32_t arg);

    // Zero-extension
    GET_FUNC(funcp, "test_zext16");
    ASSERT_EQ(funcp(0x81828384), 0x8384);

    GET_FUNC(funcp, "test_zext8");
    ASSERT_EQ(funcp(0x81828384), 0x84);

    GET_FUNC(funcp, "test_zext1");
    ASSERT_EQ(funcp(0x81828384), 0);
    ASSERT_EQ(funcp(0x81828385), 1);

    // Sign-extension
    GET_FUNC(funcp, "test_sext16");
    ASSERT_EQ(funcp(0x81828384), 0xffff8384);

    GET_FUNC(funcp, "test_sext8");
    ASSERT_EQ(funcp(0x81828384), 0xffffff84);

    GET_FUNC(funcp, "test_sext1");
    ASSERT_EQ(funcp(0x81828384), 0);
    ASSERT_EQ(funcp(0x81828385), 0xffffffff);
  }
}

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

void test_arithmetic() {
  llvm::SMDiagnostic err;
  llvm::LLVMContext &context = llvm::getGlobalContext();
  const char *filename = "gen_arithmetic_test.ll";
  llvm::Module *module = llvm::ParseIRFile(filename, err, context);
  if (!module) {
    fprintf(stderr, "failed to read file: %s\n", filename);
    assert(0);
  }

  std::map<std::string,uintptr_t> globals;
  translate(module, &globals);
  struct TestFunc *translated_test_funcs =
    (struct TestFunc *) globals["test_funcs"];

  for (int i = 0; test_funcs[i].name != NULL; ++i) {
    printf("test %s\n", test_funcs[i].name);
    int test_args[][2] = {
      { 400, 100 },
      // Good for testing multiplication and division:
      { -7, -3 },
      { 7, -3 },
      { -7, 3 },
      // For testing comparisons.
      { 123, 123 }, // equal
    };
    for (unsigned j = 0; j < ARRAY_SIZE(test_args); ++j) {
      uint32_t arg1 = test_args[j][0];
      uint32_t arg2 = test_args[j][1];
      uint32_t expected_result = 0;
      uint32_t actual_result = 0;
      test_funcs[i].func(&arg1, &arg2, &expected_result);
      translated_test_funcs[i].func(&arg1, &arg2, &actual_result);
      printf("  %i, %i -> %i\n", arg1, arg2, actual_result);
      ASSERT_EQ(expected_result, actual_result);
    }
  }
}

int main() {
  // Turn off stdout buffering to aid debugging.
  setvbuf(stdout, NULL, _IONBF, 0);

  test_features();
  test_arithmetic();

  printf("OK\n");
  return 0;
}
