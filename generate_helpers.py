
import sys


OPERATIONS = [
  'Xchg',
  'Add',
  'Sub',
  'And',
  'Nand',
  'Or',
  'Xor',
  'Max',
  'Min',
  'UMax',
  'UMin',
  ]


def main():
  if sys.argv[1] == '--ll-file':
    print 'target triple = "i386-pc-linux-gnu"'
    for op_name in OPERATIONS:
      print """
define i32 @runtime_atomicrmw_i32_%(op_name)s(\
i32* inreg %%ptr, i32 inreg %%val) {
  %%result = atomicrmw %(op_name_lc)s i32* %%ptr, i32 %%val seq_cst
  ret i32 %%result
}\
""" % {'op_name': op_name,
       'op_name_lc': op_name.lower()}

  if sys.argv[1] == '--header-file':
    print """
#ifndef RUNTIME_HELPERS_ATOMIC_H_
#define RUNTIME_HELPERS_ATOMIC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
"""

    for op_name in OPERATIONS:
      # We don't need to declare arguments for this since we only call
      # this from directly-generated code, not from C or C++.
      print 'uint32_t runtime_atomicrmw_i32_%(op_name)s();' % {
          'op_name': op_name}

    print """
#ifdef __cplusplus
}
#endif

#endif\
"""


if __name__ == '__main__':
  main()
