
#ifndef NACL_IRT_INTERFACES_H_
#define NACL_IRT_INTERFACES_H_ 1

/* This is a subset of NaCl's IRT interfaces. */

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

/* From native_client/src/include/elf32.h: */

struct Elf32_auxv {
  int a_type;
  union {
    int a_val;
  } a_un;
};

/* From native_client/src/include/elf_auxv.h: */

#define AT_SYSINFO 32

#endif
