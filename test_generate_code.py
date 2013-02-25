
OPERATORS = [
  ('add', '+'),
  ('sub', '-'),
  ('mul', '*'),
  # TODO: Enable this
  # ('div', '/'),
  ]


def main():
  print """\
/* Generated code */

#include <stdint.h>
#include <stdlib.h>

#include "arithmetic_test.h"
"""
  func_list = []
  for op_name, op in OPERATORS:
    ty = 'uint32_t'
    func_name = 'func_%s_%s' % (op_name, ty)
    code = """\
void func_%(op_name)s_%(ty)s(void *arg1, void *arg2, void *result) {
  *(%(ty)s *) result = *(%(ty)s *) arg1 %(op)s *(%(ty)s *) arg2;
}
""" % {'ty': ty,
       'op_name': op_name,
       'op': op}
    print code
    func_list.append('  { "%(func_name)s", %(func_name)s },\n'
                     % {'func_name': func_name})

  print 'struct TestFunc test_funcs[] = {'
  print ''.join(func_list),
  print '  { NULL, NULL }'
  print '};'


if __name__ == '__main__':
  main()
