
#ifndef NACL_IRT_INTERFACES_H_
#define NACL_IRT_INTERFACES_H_ 1

/* This is a subset of NaCl's IRT interfaces. */

#include <sys/types.h>

struct stat;
struct dirent;
struct timeval;
struct timespec;


/* From native_client/src/untrusted/irt/irt.h: */

#define NACL_IRT_BASIC_v0_1     "nacl-irt-basic-0.1"
struct nacl_irt_basic {
  void (*exit)(int status);
  int (*gettod)(struct timeval *tv);
  int (*clock)(clock_t *ticks);
  int (*nanosleep)(const struct timespec *req, struct timespec *rem);
  int (*sched_yield)(void);
  int (*sysconf)(int name, int *value);
};

#define NACL_IRT_FDIO_v0_1      "nacl-irt-fdio-0.1"
struct nacl_irt_fdio {
  int (*close)(int fd);
  int (*dup)(int fd, int *newfd);
  int (*dup2)(int fd, int newfd);
  int (*read)(int fd, void *buf, size_t count, size_t *nread);
  int (*write)(int fd, const void *buf, size_t count, size_t *nwrote);
  int (*seek)(int fd, off_t offset, int whence, off_t *new_offset);
  int (*fstat)(int fd, struct stat *);
  int (*getdents)(int fd, struct dirent *, size_t count, size_t *nread);
};

#define NACL_IRT_FILENAME_v0_1      "nacl-irt-filename-0.1"
struct nacl_irt_filename {
  int (*open)(const char *pathname, int oflag, mode_t cmode, int *newfd);
  int (*stat)(const char *pathname, struct stat *);
};

#define NACL_IRT_MEMORY_v0_1    "nacl-irt-memory-0.1"
struct nacl_irt_memory {
  int (*sysbrk)(void **newbrk);
  int (*mmap)(void **addr, size_t len, int prot, int flags, int fd, off_t off);
  int (*munmap)(void *addr, size_t len);
};

#define NACL_IRT_DYNCODE_v0_1   "nacl-irt-dyncode-0.1"
struct nacl_irt_dyncode {
  int (*dyncode_create)(void *dest, const void *src, size_t size);
  int (*dyncode_modify)(void *dest, const void *src, size_t size);
  int (*dyncode_delete)(void *dest, size_t size);
};

#define NACL_IRT_TLS_v0_1       "nacl-irt-tls-0.1"
struct nacl_irt_tls {
  int (*tls_init)(void *thread_ptr);
  void *(*tls_get)(void);
};

#define NACL_IRT_BLOCKHOOK_v0_1 "nacl-irt-blockhook-0.1"
struct nacl_irt_blockhook {
  int (*register_block_hooks)(void (*pre)(void), void (*post)(void));
};

typedef size_t (*TYPE_nacl_irt_query)(const char *interface_ident,
                                      void *table, size_t tablesize);


/* From native_client/src/include/elf32.h: */

typedef struct {
  int a_type;
  union {
    int a_val;
  } a_un;
} Elf32_auxv_t;


/* From native_client/src/include/elf_auxv.h: */

#define AT_NULL 0
#define AT_SYSINFO 32


/* From native_client/src/untrusted/nacl/nacl_startup.h: */

enum NaClStartupInfoIndex {
  NACL_STARTUP_FINI,  /* Cleanup function pointer for dynamic linking.  */
  NACL_STARTUP_ENVC,  /* Count of envp[] pointers.  */
  NACL_STARTUP_ARGC,  /* Count of argv[] pointers.  */
  NACL_STARTUP_ARGV   /* argv[0] pointer.  */
};

static inline int nacl_startup_argc(const uint32_t info[]) {
  return info[NACL_STARTUP_ARGC];
}

static inline char **nacl_startup_argv(const uint32_t *info) {
  return (char **) &info[NACL_STARTUP_ARGV];
}

static inline int nacl_startup_envc(const uint32_t info[]) {
  return info[NACL_STARTUP_ENVC];
}

static inline char **nacl_startup_envp(const uint32_t *info) {
  return &nacl_startup_argv(info)[nacl_startup_argc(info) + 1];
}

static inline Elf32_auxv_t *nacl_startup_auxv(const uint32_t *info) {
  char **envend = &nacl_startup_envp(info)[nacl_startup_envc(info) + 1];
  return (Elf32_auxv_t *) envend;
}

#endif
