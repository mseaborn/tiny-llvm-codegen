
#include "runtime_helpers.h"

static __thread void *tls_thread_ptr;

int runtime_tls_init(void *thread_ptr) {
  tls_thread_ptr = thread_ptr;
  return 0;
}

void *runtime_tls_get(void) {
  return tls_thread_ptr;
}

void runtime_i64_Add(uint64_t *result, uint64_t *arg1, uint64_t *arg2) {
  *result = *arg1 + *arg2;
}

void runtime_i64_Sub(uint64_t *result, uint64_t *arg1, uint64_t *arg2) {
  *result = *arg1 - *arg2;
}

void runtime_i64_Mul(uint64_t *result, uint64_t *arg1, uint64_t *arg2) {
  *result = *arg1 * *arg2;
}

void runtime_i64_UDiv(uint64_t *result, uint64_t *arg1, uint64_t *arg2) {
  *result = *arg1 / *arg2;
}

void runtime_i64_URem(uint64_t *result, uint64_t *arg1, uint64_t *arg2) {
  *result = *arg1 % *arg2;
}

void runtime_i64_SDiv(int64_t *result, int64_t *arg1, int64_t *arg2) {
  *result = *arg1 / *arg2;
}

void runtime_i64_SRem(int64_t *result, int64_t *arg1, int64_t *arg2) {
  *result = *arg1 % *arg2;
}

void runtime_i64_And(uint64_t *result, uint64_t *arg1, uint64_t *arg2) {
  *result = *arg1 & *arg2;
}

void runtime_i64_Or(uint64_t *result, uint64_t *arg1, uint64_t *arg2) {
  *result = *arg1 | *arg2;
}

void runtime_i64_Xor(uint64_t *result, uint64_t *arg1, uint64_t *arg2) {
  *result = *arg1 ^ *arg2;
}

void runtime_i64_Shl(uint64_t *result, uint64_t *arg1, uint64_t *arg2) {
  *result = *arg1 << *arg2;
}

void runtime_i64_LShr(uint64_t *result, uint64_t *arg1, uint64_t *arg2) {
  *result = *arg1 >> *arg2;
}

void runtime_i64_AShr(int64_t *result, int64_t *arg1, int64_t *arg2) {
  *result = *arg1 >> *arg2;
}

int runtime_i64_ICMP_EQ(uint64_t *arg1, uint64_t *arg2) {
  return *arg1 == *arg2;
}

int runtime_i64_ICMP_NE(uint64_t *arg1, uint64_t *arg2) {
  return *arg1 != *arg2;
}

int runtime_i64_ICMP_UGT(uint64_t *arg1, uint64_t *arg2) {
  return *arg1 > *arg2;
}

int runtime_i64_ICMP_UGE(uint64_t *arg1, uint64_t *arg2) {
  return *arg1 >= *arg2;
}

int runtime_i64_ICMP_ULT(uint64_t *arg1, uint64_t *arg2) {
  return *arg1 < *arg2;
}

int runtime_i64_ICMP_ULE(uint64_t *arg1, uint64_t *arg2) {
  return *arg1 <= *arg2;
}

int runtime_i64_ICMP_SGT(int64_t *arg1, int64_t *arg2) {
  return *arg1 > *arg2;
}

int runtime_i64_ICMP_SGE(int64_t *arg1, int64_t *arg2) {
  return *arg1 >= *arg2;
}

int runtime_i64_ICMP_SLT(int64_t *arg1, int64_t *arg2) {
  return *arg1 < *arg2;
}

int runtime_i64_ICMP_SLE(int64_t *arg1, int64_t *arg2) {
  return *arg1 <= *arg2;
}
