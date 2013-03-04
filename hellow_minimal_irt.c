
/*
 * This is a minimal NaCl "hello world" program.  It uses NaCl's
 * stable IRT ABI.
 */

#include <stdint.h>

#include "nacl_irt_interfaces.h"

struct nacl_irt_basic __libnacl_irt_basic;
struct nacl_irt_fdio __libnacl_irt_fdio;
TYPE_nacl_irt_query __nacl_irt_query;

static void grok_auxv(const Elf32_auxv_t *auxv) {
  const Elf32_auxv_t *av;
  for (av = auxv; av->a_type != AT_NULL; ++av) {
    if (av->a_type == AT_SYSINFO) {
      __nacl_irt_query = (TYPE_nacl_irt_query) av->a_un.a_val;
    }
  }
}

static void __libnacl_mandatory_irt_query(const char *interface,
                                          void *table, size_t table_size) {
  __nacl_irt_query(interface, table, table_size);
}

#define DO_QUERY(ident, name)                                   \
  __libnacl_mandatory_irt_query(ident, &__libnacl_irt_##name,   \
                                sizeof(__libnacl_irt_##name))

void _start(uint32_t *info) {
  Elf32_auxv_t *auxv = nacl_startup_auxv(info);
  grok_auxv(auxv);
  DO_QUERY(NACL_IRT_BASIC_v0_1, basic);
  DO_QUERY(NACL_IRT_FDIO_v0_1, fdio);

  const char string[] = "Hello world!\n";
  size_t written;
  __libnacl_irt_fdio.write(2, string, sizeof(string) - 1, &written);
  __libnacl_irt_basic.exit(0);
}
