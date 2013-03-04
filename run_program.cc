
#include <assert.h>
#include <stdio.h>

#include <llvm/LLVMContext.h>
#include <llvm/Support/IRReader.h>

#include "codegen.h"
#include "nacl_irt_interfaces.h"

#define NACL_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

struct nacl_irt_basic irt_basic;
struct nacl_irt_fdio irt_fdio;

int irt_write(int fd, const void *buf, size_t count, size_t *nwrote) {
  int result = write(fd, buf, count);
  if (result < 0)
    return -result;
  *nwrote = result;
  return 0;
}

void irt_exit(int status) {
  _exit(status);
}

struct nacl_interface_table {
  const char *name;
  const void *table;
  size_t size;
};

static const struct nacl_interface_table irt_interfaces[] = {
  { NACL_IRT_BASIC_v0_1, &irt_basic, sizeof(irt_basic) },
  { NACL_IRT_FDIO_v0_1, &irt_fdio, sizeof(irt_fdio) },
};

size_t irt_interface_query(const char *interface_ident,
                           void *table, size_t tablesize) {
  unsigned i;
  for (i = 0; i < NACL_ARRAY_SIZE(irt_interfaces); ++i) {
    if (0 == strcmp(interface_ident, irt_interfaces[i].name)) {
      const size_t size = irt_interfaces[i].size;
      if (size <= tablesize) {
        memcpy(table, irt_interfaces[i].table, size);
        return size;
      }
      break;
    }
  }
  return 0;
}

// Layout for empty argv/env arrays.
struct startup_info {
  void (*cleanup_func)();
  int envc;
  int argc;
  char *argv0;
  char *envp0;
  Elf32_auxv_t auxv[2];
};

int main(int argc, char **argv) {
  llvm::SMDiagnostic err;
  llvm::LLVMContext &context = llvm::getGlobalContext();

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <bitcode-file>\n", argv[0]);
    return 1;
  }
  const char *filename = argv[1];
  llvm::Module *module = llvm::ParseIRFile(filename, err, context);
  if (!module) {
    fprintf(stderr, "failed to read file: %s\n", filename);
    return 1;
  }

  std::map<std::string,uintptr_t> globals;
  translate(module, &globals);

  irt_basic.exit = irt_exit;
  irt_fdio.write = irt_write;

  struct startup_info info;
  info.cleanup_func = NULL;
  info.envc = 0;
  info.argc = 0;
  info.argv0 = NULL;
  info.envp0 = NULL;
  info.auxv[0].a_type = AT_SYSINFO;
  info.auxv[0].a_un.a_val = (uintptr_t) irt_interface_query;
  info.auxv[1].a_type = 0;
  info.auxv[1].a_un.a_val = 0;

  void (*entry)(struct startup_info *info);
  entry = (typeof(entry)) globals["_start"];
  assert(entry);
  entry(&info);

  return 0;
}
