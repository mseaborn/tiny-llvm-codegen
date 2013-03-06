
#ifndef RUNTIME_HELPERS_H_
#define RUNTIME_HELPERS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void runtime_i64_Add(uint64_t *result, uint64_t *arg1, uint64_t *arg2);
void runtime_i64_Sub(uint64_t *result, uint64_t *arg1, uint64_t *arg2);
void runtime_i64_Mul(uint64_t *result, uint64_t *arg1, uint64_t *arg2);
void runtime_i64_UDiv(uint64_t *result, uint64_t *arg1, uint64_t *arg2);
void runtime_i64_URem(uint64_t *result, uint64_t *arg1, uint64_t *arg2);
void runtime_i64_SDiv(int64_t *result, int64_t *arg1, int64_t *arg2);
void runtime_i64_SRem(int64_t *result, int64_t *arg1, int64_t *arg2);

void runtime_i64_And(uint64_t *result, uint64_t *arg1, uint64_t *arg2);
void runtime_i64_Or(uint64_t *result, uint64_t *arg1, uint64_t *arg2);
void runtime_i64_Xor(uint64_t *result, uint64_t *arg1, uint64_t *arg2);
void runtime_i64_Shl(uint64_t *result, uint64_t *arg1, uint64_t *arg2);
void runtime_i64_LShr(uint64_t *result, uint64_t *arg1, uint64_t *arg2);
void runtime_i64_AShr(int64_t *result, int64_t *arg1, int64_t *arg2);

#ifdef __cplusplus
}
#endif

#endif
