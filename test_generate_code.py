
OPERATORS = [
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


def main():
  print """\
/* Generated code */

#include <stdint.h>
#include <stdlib.h>

#include "arithmetic_test.h"
"""
  func_list = []
  for sign in ('u', ''):
    for int_size in (64, 32, 16, 8):
      for op_name, op in OPERATORS:
        if op_name in ('eq', 'ne', 'gt', 'ge', 'lt', 'le') and int_size == 64:
          # TODO: Handle comparisons on i64.
          continue
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

  print 'struct TestFunc test_funcs[] = {'
  print ''.join(func_list),
  print '  { NULL, NULL }'
  print '};'


if __name__ == '__main__':
  main()
