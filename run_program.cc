
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

#include <llvm/LLVMContext.h>
#include <llvm/Support/IRReader.h>

#include "codegen.h"
#include "nacl_irt_interfaces.h"
#include "runtime_helpers.h"

#define NACL_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static void *g_sysbrk_current;
static void *g_sysbrk_max;

static int irt_close(int fd) {
  // Ignore close() for now because newlib closes stdin/stdout/stderr
  // on exit, which is not very helpful if we want to debug the
  // runtime at that point.
  return -ENOSYS;
}

static int irt_write(int fd, const void *buf, size_t count, size_t *nwrote) {
  int result = write(fd, buf, count);
  if (result < 0)
    return -result;
  *nwrote = result;
  return 0;
}

static int irt_fstat(int fd, struct stat *st) {
  return -ENOSYS;
}

static void irt_exit(int status) {
  _exit(status);
}

enum {
  NACL_ABI__SC_SENDMSG_MAX_SIZE,
  NACL_ABI__SC_NPROCESSORS_ONLN,
  NACL_ABI__SC_PAGESIZE,
  NACL_ABI__SC_LAST
};

static int irt_sysconf(int name, int *value) {
  switch (name) {
    case NACL_ABI__SC_PAGESIZE:
      *value = 0x10000;
      return 0;
    default:
      return -EINVAL;
  }
}

static int irt_sysbrk(void **brk) {
  if (!g_sysbrk_current) {
    // The brk area is inherently limited, so having a cap here is
    // somewhat reasonable, although it's wasteful to allocate a big
    // chunk up front.
    int size = 16 << 20; // 16MB
    g_sysbrk_current = mmap(NULL, size, PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(g_sysbrk_current != MAP_FAILED);
    g_sysbrk_max = (void *) ((char *) g_sysbrk_current + size);
  }
  if (*brk == NULL) {
    *brk = g_sysbrk_current;
  } else {
    assert(*brk <= g_sysbrk_max);
    g_sysbrk_current = *brk;
  }
  return 0;
}

static void irt_stub_func(const char *name) {
  fprintf(stderr, "Error: Unimplemented IRT function: %s\n", name);
  abort();
}

#define DEFINE_STUB(name) \
    static void irt_stub_##name() { irt_stub_func(#name); }
#define USE_STUB(s, name) (typeof(s.name)) irt_stub_##name

DEFINE_STUB(gettod)
DEFINE_STUB(clock)
DEFINE_STUB(nanosleep)
DEFINE_STUB(sched_yield)
struct nacl_irt_basic irt_basic = {
  irt_exit,
  USE_STUB(irt_basic, gettod),
  USE_STUB(irt_basic, clock),
  USE_STUB(irt_basic, nanosleep),
  USE_STUB(irt_basic, sched_yield),
  irt_sysconf,
};

DEFINE_STUB(dup)
DEFINE_STUB(dup2)
DEFINE_STUB(read)
DEFINE_STUB(seek)
DEFINE_STUB(getdents)
struct nacl_irt_fdio irt_fdio = {
  irt_close,
  USE_STUB(irt_fdio, dup),
  USE_STUB(irt_fdio, dup2),
  USE_STUB(irt_fdio, read),
  irt_write,
  USE_STUB(irt_fdio, seek),
  irt_fstat,
  USE_STUB(irt_fdio, getdents),
};

DEFINE_STUB(open)
DEFINE_STUB(stat)
struct nacl_irt_filename irt_filename = {
  USE_STUB(irt_filename, open),
  USE_STUB(irt_filename, stat),
};

DEFINE_STUB(mmap)
DEFINE_STUB(munmap)
struct nacl_irt_memory irt_memory = {
  irt_sysbrk,
  USE_STUB(irt_memory, mmap),
  USE_STUB(irt_memory, munmap),
};

DEFINE_STUB(dyncode_create)
DEFINE_STUB(dyncode_modify)
DEFINE_STUB(dyncode_delete)
struct nacl_irt_dyncode irt_dyncode = {
  USE_STUB(irt_dyncode, dyncode_create),
  USE_STUB(irt_dyncode, dyncode_modify),
  USE_STUB(irt_dyncode, dyncode_delete),
};

struct nacl_irt_tls irt_tls = {
  runtime_tls_init,
  runtime_tls_get,
};

DEFINE_STUB(register_block_hooks)
struct nacl_irt_blockhook irt_blockhook = {
  USE_STUB(irt_blockhook, register_block_hooks),
};

struct nacl_interface_table {
  const char *name;
  const void *table;
  size_t size;
};

static const struct nacl_interface_table irt_interfaces[] = {
  { NACL_IRT_BASIC_v0_1, &irt_basic, sizeof(irt_basic) },
  { NACL_IRT_FDIO_v0_1, &irt_fdio, sizeof(irt_fdio) },
  { NACL_IRT_FILENAME_v0_1, &irt_filename, sizeof(irt_filename) },
  { NACL_IRT_MEMORY_v0_1, &irt_memory, sizeof(irt_memory) },
  { NACL_IRT_DYNCODE_v0_1, &irt_dyncode, sizeof(irt_dyncode) },
  { NACL_IRT_TLS_v0_1, &irt_tls, sizeof(irt_tls) },
  { NACL_IRT_BLOCKHOOK_v0_1, &irt_blockhook, sizeof(irt_blockhook) },
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
  fprintf(stderr, "Warning: unavailable IRT interface queried: %s\n",
          interface_ident);
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

  CodeGenOptions options;
  const char *prog_name = argv[0];
  int arg = 1;
  while (arg < argc) {
    if (!strcmp(argv[arg], "--dump")) {
      options.dump_code = true;
      arg++;
    } else if (!strcmp(argv[arg], "--trace")) {
      options.trace_logging = true;
      arg++;
    } else {
      break;
    }
  }

  if (arg + 1 != argc) {
    fprintf(stderr, "Usage: %s <bitcode-file>\n", prog_name);
    return 1;
  }
  const char *filename = argv[arg];
  llvm::Module *module = llvm::ParseIRFile(filename, err, context);
  if (!module) {
    fprintf(stderr, "failed to read file: %s\n", filename);
    return 1;
  }

  std::map<std::string,uintptr_t> globals;
  translate(module, &globals, &options);

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
