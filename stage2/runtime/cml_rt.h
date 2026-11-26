#ifndef CML_RT_H
#define CML_RT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CmlObj* CmlVal;
typedef CmlVal (*CmlFnPtr)(CmlVal* env, CmlVal* args);

void cml_rt_init(int argc, char** argv);

/* Constructors */
CmlVal cml_unit(void);
CmlVal cml_int(int64_t v);
CmlVal cml_float(double v);
CmlVal cml_bool(int64_t b);
CmlVal cml_str(const char* s, int64_t len);
CmlVal cml_cstr(const char* s);
CmlVal cml_nil(void);
CmlVal cml_cons(CmlVal hd, CmlVal tl);
CmlVal cml_tuple_make(int64_t n, CmlVal* items);
CmlVal cml_list_make(int64_t n, CmlVal* items);
CmlVal cml_adt_make(int64_t tag, const char* name, int64_t n, CmlVal* fields);
CmlVal cml_struct_make(const char* name, int64_t n, const char** fnames, CmlVal* fvals);
CmlVal cml_closure(CmlFnPtr fn, int64_t arity, int64_t nenv, CmlVal* env);
void cml_closure_set_env(CmlVal clo, int64_t idx, CmlVal val);

/* Call & Apply */
CmlVal cml_apply1(CmlVal fn, CmlVal arg);
CmlVal cml_apply_n(CmlVal fn, int64_t n, CmlVal* args);
int64_t cml_is_true(CmlVal v);

/* Operators */
CmlVal cml_neg(CmlVal a);
CmlVal cml_fneg(CmlVal a);
CmlVal cml_bitnot(CmlVal a);
CmlVal cml_not(CmlVal a);
CmlVal cml_add(CmlVal a, CmlVal b);
CmlVal cml_sub(CmlVal a, CmlVal b);
CmlVal cml_mul(CmlVal a, CmlVal b);
CmlVal cml_div(CmlVal a, CmlVal b);
CmlVal cml_mod(CmlVal a, CmlVal b);
CmlVal cml_fadd(CmlVal a, CmlVal b);
CmlVal cml_fsub(CmlVal a, CmlVal b);
CmlVal cml_fmul(CmlVal a, CmlVal b);
CmlVal cml_fdiv(CmlVal a, CmlVal b);
CmlVal cml_bitand(CmlVal a, CmlVal b);
CmlVal cml_bitor(CmlVal a, CmlVal b);
CmlVal cml_bitxor(CmlVal a, CmlVal b);
CmlVal cml_shl(CmlVal a, CmlVal b);
CmlVal cml_shr(CmlVal a, CmlVal b);
CmlVal cml_concat(CmlVal a, CmlVal b);
CmlVal cml_eq(CmlVal a, CmlVal b);
CmlVal cml_neq(CmlVal a, CmlVal b);
CmlVal cml_lt(CmlVal a, CmlVal b);
CmlVal cml_le(CmlVal a, CmlVal b);
CmlVal cml_gt(CmlVal a, CmlVal b);
CmlVal cml_ge(CmlVal a, CmlVal b);

/* Fields & Heap Pointers */
CmlVal cml_field_get(CmlVal obj, const char* fld);
CmlVal cml_field_set(CmlVal obj, const char* fld, CmlVal val);
CmlVal cml_new(CmlVal val);
CmlVal cml_close(CmlVal ptr);
CmlVal cml_deref(CmlVal ptr);
CmlVal cml_set_deref(CmlVal ptr, CmlVal val);
CmlVal cml_ptr_get(CmlVal ptr, const char* fld);
CmlVal cml_ptr_set(CmlVal ptr, const char* fld, CmlVal val);
CmlVal cml_addr_of_cell(CmlVal* cell);
void cml_invalidate_cell(CmlVal ptr);

/* Pattern Matching */
int64_t cml_match_int(CmlVal v, int64_t target);
int64_t cml_match_float(CmlVal v, double target);
int64_t cml_match_bool(CmlVal v, int64_t target);
int64_t cml_match_str(CmlVal v, const char* s, int64_t len);
int64_t cml_match_unit(CmlVal v);
int64_t cml_match_tuple(CmlVal v, int64_t arity);
int64_t cml_match_list_len(CmlVal v, int64_t len);
int64_t cml_match_cons(CmlVal v);
int64_t cml_match_adt(CmlVal v, int64_t tag);
CmlVal cml_tuple_get(CmlVal v, int64_t idx);
CmlVal cml_list_get(CmlVal v, int64_t idx);
CmlVal cml_list_tail(CmlVal v, int64_t drop);
CmlVal cml_adt_field(CmlVal v, int64_t idx);
void cml_match_fail(void);

/* Builtin globals factory */
CmlVal cml_builtin_closure(const char* name);

#ifdef __cplusplus
}
#endif

#endif
