
import sys

# This generates test functions that use arithmetic and comparison
# operations for many size types.
#
# There are two variants:  C and LLVM code generators.
#
# The C code generator is simpler, because C is less verbose than
# LLVM.  It has the advantage of not requiring another LLVM code
# generator to test against.
#
# However, the coverage of the C code generator is limited: It cannot
# generate i1 arithmetic operations.  It is not guaranteed to generate
# i8 and i16 operations, because C arithmetic is always promoted to
# "int", and whether these i32 operations are converted to i8/i16
# depends on optimization passes.


C_OPERATORS = [
  # Arithmetic
  ('add', '+'),
  ('sub', '-'),
  ('mul', '*'),
  ('div', '/'),
  ('rem', '%'),
  # Bitwise operations
  ('and', '&'),
  ('or', '|'),
  ('xor', '^'),
  ('shift_left', '<<'),
  ('shift_right', '>>'),
  # Comparisons
  ('eq', '=='),
  ('ne', '!='),
  ('gt', '>'),
  ('ge', '>='),
  ('lt', '<'),
  ('le', '<='),
  ]


# TODO: Test comparisons too
LLVM_OPERATORS = [
  'add', 'sub', 'mul',
  'udiv', 'urem',
  'sdiv', 'srem',
  'and', 'or', 'xor',
  'shl',
  'lshr', 'ashr',
  ]


def generate_c():
  print """\
/* Generated code */

#include <stdint.h>
#include <stdlib.h>

#include "arithmetic_test.h"
"""
  func_list = []
  for sign in ('u', ''):
    for int_size in (64, 32, 16, 8):
      for op_name, op in C_OPERATORS:
        ty = '%sint%i_t' % (sign, int_size)
        func_name = 'func_%s_%s' % (op_name, ty)
        args = {'ty': ty,
                'op_name': op_name,
                'op': op}
        code = """\
void func_%(op_name)s_%(ty)s(void *arg1, void *arg2, void *result) {
  *(%(ty)s *) result = *(%(ty)s *) arg1 %(op)s *(%(ty)s *) arg2;
}
""" % args
        print code
        func_list.append('  { "%(func_name)s", %(func_name)s },\n'
                         % {'func_name': func_name})

  print 'struct TestFunc test_funcs_c[] = {'
  print ''.join(func_list),
  print '  { NULL, NULL }'
  print '};'


def generate_ll():
  # Generating LLVM textual assembly is rather clumsy because the type
  # names are duplicated so often.  It might be cleaner to do this
  # generation using LLVM's C++ API, though the code for that would
  # probably be more verbose.
  print "; Generated code\n"
  print 'target triple = "i386-pc-linux-gnu"\n'
  print 'target datalayout = "p:32:32:32"'
  func_names = []
  # TODO: Test i64 too.  Testing i64 requires omitting the trunc/zext
  # operations below.
  for int_size in (32, 16, 8, 1):
    for op_name in LLVM_OPERATORS:
      if int_size == 1 and op_name not in ('and', 'or', 'xor'):
        # Operations other than logic operations on i1 are somewhat
        # dubious.  They are hard to test because of the high
        # likelihood of generating undefined behaviour (for bit
        # shifting) or division-by-zero.
        continue
      ty = 'i%i' % int_size
      func_name = 'func_%s_%s' % (op_name, ty)
      print '@name_%s = constant [%i x i8] c"%s\\00"' % (
          func_name, len(func_name) + 1, func_name)
      template = """\
define void @%(func)s(i64* %%argptr1, i64* %%argptr2, i64* %%resultptr) {
  %%val1 = load i64* %%argptr1
  %%val2 = load i64* %%argptr2
  %%trunc1 = trunc i64 %%val1 to %(ty)s
  %%trunc2 = trunc i64 %%val2 to %(ty)s
  %%result = %(op_name)s %(ty)s %%trunc1, %%trunc2
  %%ext = zext %(ty)s %%result to i64
  store i64 %%ext, i64* %%resultptr
  ret void
}
"""
      print template % {'ty': ty,
                        'op_name': op_name,
                        'func': func_name}
      func_names.append(func_name)

  print '%TestFunc = type { i8*, i8* }'
  print '@test_funcs_ll = constant [%i x %%TestFunc] [' % (len(func_names) + 1)
  for name in func_names:
    name_expr = 'getelementptr ([%i x i8]* @name_%s, i32 0, i32 0)' % (
        len(name) + 1, name)
    func_expr = 'bitcast (void (i64*, i64*, i64*)* @%s to i8*)' % name
    print '  %%TestFunc { i8* %s, i8* %s },' % (name_expr, func_expr)
  print '  %TestFunc { i8* null, i8* null }'
  print ']'


def main():
  if sys.argv[1] == '--c-file':
    generate_c()
  elif sys.argv[1] == '--ll-file':
    generate_ll()
  else:
    raise AssertionError('Unknown arguments')


if __name__ == '__main__':
  main()
