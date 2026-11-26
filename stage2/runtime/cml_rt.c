#include "cml_rt.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TAG_UNIT = 0,
    TAG_INT,
    TAG_FLOAT,
    TAG_BOOL,
    TAG_STR,
    TAG_TUPLE,
    TAG_NIL,
    TAG_CONS,
    TAG_ADT,
    TAG_STRUCT,
    TAG_HEAP_PTR,
    TAG_CLOSURE,
    TAG_VEC,
    TAG_DICT,
    TAG_SET
} CmlTag;

typedef struct CmlObj {
    CmlTag tag;
    union {
        int64_t i;
        double f;
        struct {
            char* data;
            int64_t len;
        } str;
        struct {
            int64_t len;
            CmlVal* items;
        } tuple;
        struct {
            CmlVal head;
            CmlVal tail;
            int64_t len;
        } cons;
        struct {
            int64_t tag_id;
            char* name;
            int64_t arity;
            CmlVal* fields;
        } adt;
        struct {
            char* name;
            int64_t nfields;
            char** fnames;
            CmlVal* fvals;
        } st;
        struct {
            int64_t alive;
            int64_t is_stack;
            CmlVal val;
            CmlVal* slot;
        } ptr;
        struct {
            CmlFnPtr fn;
            int64_t arity;
            int64_t nenv;
            CmlVal* env;
            int64_t nbound;
            CmlVal* bound;
        } clo;
        struct {
            int64_t len;
            int64_t cap;
            CmlVal* items;
        } vec;
        struct {
            int64_t len;
            int64_t cap;
            CmlVal* keys;
            CmlVal* vals;
        } dict;
        struct {
            int64_t len;
            int64_t cap;
            CmlVal* items;
        } set;
    } u;
} CmlObj;

static struct CmlObj G_UNIT = {TAG_UNIT, {0}};
static struct CmlObj G_TRUE = {TAG_BOOL, {.i = 1}};
static struct CmlObj G_FALSE = {TAG_BOOL, {.i = 0}};
static struct CmlObj G_NIL = {TAG_NIL, {0}};

static int64_t g_heap_live = 0;
static int g_argc = 0;
static char** g_argv = NULL;

static void rt_panic(const char* msg) {
    fprintf(stderr, "runtime error: %s\n", msg);
    exit(1);
}

static CmlObj* alloc_obj(CmlTag tag) {
    CmlObj* o = (CmlObj*)calloc(1, sizeof(CmlObj));
    if (!o) rt_panic("out of memory");
    o->tag = tag;
    return o;
}

static char* xstrdup(const char* s) {
    size_t n = strlen(s);
    char* p = (char*)malloc(n + 1);
    if (!p) rt_panic("out of memory");
    memcpy(p, s, n + 1);
    return p;
}

void cml_rt_init(int argc, char** argv) {
    g_argc = argc;
    g_argv = argv;
}

CmlVal cml_unit(void) { return &G_UNIT; }
CmlVal cml_bool(int64_t b) { return b ? &G_TRUE : &G_FALSE; }
CmlVal cml_nil(void) { return &G_NIL; }

CmlVal cml_int(int64_t v) {
    CmlObj* o = alloc_obj(TAG_INT);
    o->u.i = v;
    return o;
}

CmlVal cml_float(double v) {
    CmlObj* o = alloc_obj(TAG_FLOAT);
    o->u.f = v;
    return o;
}

CmlVal cml_str(const char* s, int64_t len) {
    CmlObj* o = alloc_obj(TAG_STR);
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) rt_panic("out of memory");
    if (len > 0 && s) memcpy(buf, s, (size_t)len);
    buf[len] = '\0';
    o->u.str.data = buf;
    o->u.str.len = len;
    return o;
}

CmlVal cml_cstr(const char* s) {
    return cml_str(s ? s : "", s ? (int64_t)strlen(s) : 0);
}

CmlVal cml_cons(CmlVal hd, CmlVal tl) {
    CmlObj* o = alloc_obj(TAG_CONS);
    o->u.cons.head = hd;
    o->u.cons.tail = tl;
    int64_t tlen = (tl->tag == TAG_NIL) ? 0 : (tl->tag == TAG_CONS ? tl->u.cons.len : 0);
    o->u.cons.len = 1 + tlen;
    return o;
}

CmlVal cml_tuple_make(int64_t n, CmlVal* items) {
    CmlObj* o = alloc_obj(TAG_TUPLE);
    o->u.tuple.len = n;
    o->u.tuple.items = (CmlVal*)malloc(sizeof(CmlVal) * (size_t)(n > 0 ? n : 1));
    for (int64_t i = 0; i < n; ++i) o->u.tuple.items[i] = items[i];
    return o;
}

CmlVal cml_list_make(int64_t n, CmlVal* items) {
    CmlVal acc = cml_nil();
    for (int64_t i = n - 1; i >= 0; --i) {
        acc = cml_cons(items[i], acc);
    }
    return acc;
}

CmlVal cml_adt_make(int64_t tag, const char* name, int64_t n, CmlVal* fields) {
    CmlObj* o = alloc_obj(TAG_ADT);
    o->u.adt.tag_id = tag;
    o->u.adt.name = xstrdup(name);
    o->u.adt.arity = n;
    if (n > 0) {
        o->u.adt.fields = (CmlVal*)malloc(sizeof(CmlVal) * (size_t)n);
        for (int64_t i = 0; i < n; ++i) o->u.adt.fields[i] = fields[i];
    }
    return o;
}

CmlVal cml_struct_make(const char* name, int64_t n, const char** fnames, CmlVal* fvals) {
    CmlObj* o = alloc_obj(TAG_STRUCT);
    o->u.st.name = xstrdup(name);
    o->u.st.nfields = n;
    o->u.st.fnames = (char**)malloc(sizeof(char*) * (size_t)(n > 0 ? n : 1));
    o->u.st.fvals = (CmlVal*)malloc(sizeof(CmlVal) * (size_t)(n > 0 ? n : 1));
    for (int64_t i = 0; i < n; ++i) {
        o->u.st.fnames[i] = xstrdup(fnames[i]);
        o->u.st.fvals[i] = fvals[i];
    }
    return o;
}

CmlVal cml_closure(CmlFnPtr fn, int64_t arity, int64_t nenv, CmlVal* env) {
    CmlObj* o = alloc_obj(TAG_CLOSURE);
    o->u.clo.fn = fn;
    o->u.clo.arity = arity;
    o->u.clo.nenv = nenv;
    if (nenv > 0 && env) {
        o->u.clo.env = (CmlVal*)malloc(sizeof(CmlVal) * (size_t)nenv);
        for (int64_t i = 0; i < nenv; ++i) o->u.clo.env[i] = env[i];
    }
    o->u.clo.nbound = 0;
    o->u.clo.bound = NULL;
    return o;
}

void cml_closure_set_env(CmlVal clo, int64_t idx, CmlVal val) {
    if (clo && clo->tag == TAG_CLOSURE && idx >= 0 && idx < clo->u.clo.nenv) {
        clo->u.clo.env[idx] = val;
    }
}

CmlVal cml_apply1(CmlVal fn, CmlVal arg) {
    if (!fn || fn->tag != TAG_CLOSURE) rt_panic("attempt to call non-function value");
    int64_t ar = fn->u.clo.arity;
    int64_t nb = fn->u.clo.nbound;
    if (ar <= 1) {
        CmlVal a[1] = {arg};
        return fn->u.clo.fn(fn->u.clo.env, a);
    }
    if (nb + 1 < ar) {
        CmlObj* o = alloc_obj(TAG_CLOSURE);
        o->u.clo.fn = fn->u.clo.fn;
        o->u.clo.arity = ar;
        o->u.clo.nenv = fn->u.clo.nenv;
        o->u.clo.env = fn->u.clo.env;
        o->u.clo.nbound = nb + 1;
        o->u.clo.bound = (CmlVal*)malloc(sizeof(CmlVal) * (size_t)(nb + 1));
        for (int64_t i = 0; i < nb; ++i) o->u.clo.bound[i] = fn->u.clo.bound[i];
        o->u.clo.bound[nb] = arg;
        return o;
    }
    CmlVal* args = (CmlVal*)malloc(sizeof(CmlVal) * (size_t)ar);
    for (int64_t i = 0; i < nb; ++i) args[i] = fn->u.clo.bound[i];
    args[nb] = arg;
    CmlVal res = fn->u.clo.fn(fn->u.clo.env, args);
    free(args);
    return res;
}

CmlVal cml_apply_n(CmlVal fn, int64_t n, CmlVal* args) {
    CmlVal cur = fn;
    for (int64_t i = 0; i < n; ++i) {
        cur = cml_apply1(cur, args[i]);
    }
    return cur;
}

int64_t cml_is_true(CmlVal v) {
    if (!v || v->tag != TAG_BOOL) rt_panic("expected Bool");
    return v->u.i ? 1 : 0;
}

/* Formatting & String Conversion */
typedef struct {
    char* buf;
    size_t len;
    size_t cap;
} StrBuf;

static void sb_append(StrBuf* sb, const char* s, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t nc = sb->cap ? sb->cap * 2 : 64;
        while (nc < sb->len + n + 1) nc *= 2;
        sb->buf = (char*)realloc(sb->buf, nc);
        sb->cap = nc;
    }
    if (n > 0) memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

static void sb_cstr(StrBuf* sb, const char* s) { sb_append(sb, s, strlen(s)); }

static void format_val_rec(StrBuf* sb, CmlVal v, int quote_str) {
    if (!v) { sb_cstr(sb, "()"); return; }
    char tmp[128];
    switch (v->tag) {
        case TAG_UNIT: sb_cstr(sb, "()"); break;
        case TAG_INT:
            snprintf(tmp, sizeof(tmp), "%lld", (long long)v->u.i);
            sb_cstr(sb, tmp);
            break;
        case TAG_FLOAT:
            snprintf(tmp, sizeof(tmp), "%.15g", v->u.f);
            sb_cstr(sb, tmp);
            break;
        case TAG_BOOL:
            sb_cstr(sb, v->u.i ? "true" : "false");
            break;
        case TAG_STR:
            if (!quote_str) {
                sb_append(sb, v->u.str.data, (size_t)v->u.str.len);
            } else {
                sb_cstr(sb, "\"");
                for (int64_t i = 0; i < v->u.str.len; ++i) {
                    unsigned char ch = (unsigned char)v->u.str.data[i];
                    if (ch == '\n') sb_cstr(sb, "\\n");
                    else if (ch == '\t') sb_cstr(sb, "\\t");
                    else if (ch == '\r') sb_cstr(sb, "\\r");
                    else if (ch == '\\') sb_cstr(sb, "\\\\");
                    else if (ch == '"') sb_cstr(sb, "\\\"");
                    else sb_append(sb, (char*)&ch, 1);
                }
                sb_cstr(sb, "\"");
            }
            break;
        case TAG_TUPLE:
            sb_cstr(sb, "(");
            for (int64_t i = 0; i < v->u.tuple.len; ++i) {
                if (i > 0) sb_cstr(sb, ", ");
                format_val_rec(sb, v->u.tuple.items[i], 1);
            }
            sb_cstr(sb, ")");
            break;
        case TAG_NIL:
            sb_cstr(sb, "[]");
            break;
        case TAG_CONS: {
            sb_cstr(sb, "[");
            CmlVal cur = v;
            int first = 1;
            while (cur->tag == TAG_CONS) {
                if (!first) sb_cstr(sb, ", ");
                first = 0;
                format_val_rec(sb, cur->u.cons.head, 1);
                cur = cur->u.cons.tail;
            }
            sb_cstr(sb, "]");
            break;
        }
        case TAG_ADT:
            if (v->u.adt.arity == 0) {
                sb_cstr(sb, v->u.adt.name);
            } else {
                sb_cstr(sb, v->u.adt.name);
                for (int64_t i = 0; i < v->u.adt.arity; ++i) {
                    sb_cstr(sb, " ");
                    format_val_rec(sb, v->u.adt.fields[i], 1);
                }
            }
            break;
        case TAG_STRUCT:
            sb_cstr(sb, v->u.st.name);
            sb_cstr(sb, " { ");
            for (int64_t i = 0; i < v->u.st.nfields; ++i) {
                if (i > 0) sb_cstr(sb, ", ");
                sb_cstr(sb, v->u.st.fnames[i]);
                sb_cstr(sb, " = ");
                format_val_rec(sb, v->u.st.fvals[i], 1);
            }
            sb_cstr(sb, " }");
            break;
        case TAG_HEAP_PTR:
            snprintf(tmp, sizeof(tmp), "<ptr:%p>", (void*)v);
            sb_cstr(sb, tmp);
            break;
        case TAG_CLOSURE:
            sb_cstr(sb, "<fn>");
            break;
        default:
            sb_cstr(sb, "<obj>");
            break;
    }
}

static char* val_to_cstr(CmlVal v, int quote_top_str, int64_t* out_len) {
    StrBuf sb = {NULL, 0, 0};
    sb_append(&sb, "", 0);
    format_val_rec(&sb, v, quote_top_str);
    if (out_len) *out_len = (int64_t)sb.len;
    return sb.buf;
}

static int vals_equal(CmlVal a, CmlVal b) {
    if (a == b) return 1;
    if (!a || !b || a->tag != b->tag) return 0;
    switch (a->tag) {
        case TAG_UNIT:
        case TAG_NIL:
            return 1;
        case TAG_INT:
        case TAG_BOOL:
            return a->u.i == b->u.i;
        case TAG_FLOAT:
            return a->u.f == b->u.f;
        case TAG_STR:
            return a->u.str.len == b->u.str.len &&
                   (a->u.str.len == 0 || memcmp(a->u.str.data, b->u.str.data, (size_t)a->u.str.len) == 0);
        case TAG_TUPLE:
            if (a->u.tuple.len != b->u.tuple.len) return 0;
            for (int64_t i = 0; i < a->u.tuple.len; ++i) {
                if (!vals_equal(a->u.tuple.items[i], b->u.tuple.items[i])) return 0;
            }
            return 1;
        case TAG_CONS:
            return a->u.cons.len == b->u.cons.len &&
                   vals_equal(a->u.cons.head, b->u.cons.head) &&
                   vals_equal(a->u.cons.tail, b->u.cons.tail);
        case TAG_ADT:
            if (a->u.adt.tag_id != b->u.adt.tag_id || a->u.adt.arity != b->u.adt.arity) return 0;
            for (int64_t i = 0; i < a->u.adt.arity; ++i) {
                if (!vals_equal(a->u.adt.fields[i], b->u.adt.fields[i])) return 0;
            }
            return 1;
        case TAG_STRUCT:
            if (a->u.st.nfields != b->u.st.nfields) return 0;
            for (int64_t i = 0; i < a->u.st.nfields; ++i) {
                if (!vals_equal(a->u.st.fvals[i], b->u.st.fvals[i])) return 0;
            }
            return 1;
        default:
            return 0;
    }
}

static int vals_cmp(CmlVal a, CmlVal b) {
    if (a->tag == TAG_INT && b->tag == TAG_INT) return (a->u.i > b->u.i) - (a->u.i < b->u.i);
    if (a->tag == TAG_FLOAT && b->tag == TAG_FLOAT) return (a->u.f > b->u.f) - (a->u.f < b->u.f);
    if (a->tag == TAG_STR && b->tag == TAG_STR) {
        int64_t m = a->u.str.len < b->u.str.len ? a->u.str.len : b->u.str.len;
        int c = m > 0 ? memcmp(a->u.str.data, b->u.str.data, (size_t)m) : 0;
        if (c != 0) return c;
        return (a->u.str.len > b->u.str.len) - (a->u.str.len < b->u.str.len);
    }
    if (a->tag == TAG_BOOL && b->tag == TAG_BOOL) return (a->u.i > b->u.i) - (a->u.i < b->u.i);
    rt_panic("cannot compare values");
    return 0;
}

/* Operators */
CmlVal cml_neg(CmlVal a) { return cml_int(-a->u.i); }
CmlVal cml_fneg(CmlVal a) { return cml_float(-a->u.f); }
CmlVal cml_bitnot(CmlVal a) { return cml_int(~a->u.i); }
CmlVal cml_not(CmlVal a) { return cml_bool(!a->u.i); }
CmlVal cml_add(CmlVal a, CmlVal b) { return cml_int(a->u.i + b->u.i); }
CmlVal cml_sub(CmlVal a, CmlVal b) { return cml_int(a->u.i - b->u.i); }
CmlVal cml_mul(CmlVal a, CmlVal b) { return cml_int(a->u.i * b->u.i); }
CmlVal cml_div(CmlVal a, CmlVal b) {
    if (b->u.i == 0) rt_panic("division by zero");
    return cml_int(a->u.i / b->u.i);
}
CmlVal cml_mod(CmlVal a, CmlVal b) {
    if (b->u.i == 0) rt_panic("modulo by zero");
    return cml_int(a->u.i % b->u.i);
}
CmlVal cml_fadd(CmlVal a, CmlVal b) { return cml_float(a->u.f + b->u.f); }
CmlVal cml_fsub(CmlVal a, CmlVal b) { return cml_float(a->u.f - b->u.f); }
CmlVal cml_fmul(CmlVal a, CmlVal b) { return cml_float(a->u.f * b->u.f); }
CmlVal cml_fdiv(CmlVal a, CmlVal b) { return cml_float(a->u.f / b->u.f); }
CmlVal cml_bitand(CmlVal a, CmlVal b) { return cml_int(a->u.i & b->u.i); }
CmlVal cml_bitor(CmlVal a, CmlVal b) { return cml_int(a->u.i | b->u.i); }
CmlVal cml_bitxor(CmlVal a, CmlVal b) { return cml_int(a->u.i ^ b->u.i); }
CmlVal cml_shl(CmlVal a, CmlVal b) { return cml_int(a->u.i << b->u.i); }
CmlVal cml_shr(CmlVal a, CmlVal b) { return cml_int(a->u.i >> b->u.i); }

CmlVal cml_concat(CmlVal a, CmlVal b) {
    if (a->tag != TAG_STR || b->tag != TAG_STR) rt_panic("++ requires strings");
    int64_t len = a->u.str.len + b->u.str.len;
    CmlObj* o = alloc_obj(TAG_STR);
    char* buf = (char*)malloc((size_t)len + 1);
    if (a->u.str.len > 0) memcpy(buf, a->u.str.data, (size_t)a->u.str.len);
    if (b->u.str.len > 0) memcpy(buf + a->u.str.len, b->u.str.data, (size_t)b->u.str.len);
    buf[len] = '\0';
    o->u.str.data = buf;
    o->u.str.len = len;
    return o;
}

CmlVal cml_eq(CmlVal a, CmlVal b) { return cml_bool(vals_equal(a, b)); }
CmlVal cml_neq(CmlVal a, CmlVal b) { return cml_bool(!vals_equal(a, b)); }
CmlVal cml_lt(CmlVal a, CmlVal b) { return cml_bool(vals_cmp(a, b) < 0); }
CmlVal cml_le(CmlVal a, CmlVal b) { return cml_bool(vals_cmp(a, b) <= 0); }
CmlVal cml_gt(CmlVal a, CmlVal b) { return cml_bool(vals_cmp(a, b) > 0); }
CmlVal cml_ge(CmlVal a, CmlVal b) { return cml_bool(vals_cmp(a, b) >= 0); }

/* Fields & Heap Pointers */
CmlVal cml_field_get(CmlVal obj, const char* fld) {
    if (!obj || obj->tag != TAG_STRUCT) rt_panic("field access on non-struct");
    for (int64_t i = 0; i < obj->u.st.nfields; ++i) {
        if (strcmp(obj->u.st.fnames[i], fld) == 0) return obj->u.st.fvals[i];
    }
    rt_panic("unknown struct field");
    return cml_unit();
}

CmlVal cml_field_set(CmlVal obj, const char* fld, CmlVal val) {
    if (!obj || obj->tag != TAG_STRUCT) rt_panic("field assign on non-struct");
    for (int64_t i = 0; i < obj->u.st.nfields; ++i) {
        if (strcmp(obj->u.st.fnames[i], fld) == 0) {
            obj->u.st.fvals[i] = val;
            return cml_unit();
        }
    }
    rt_panic("unknown struct field");
    return cml_unit();
}

CmlVal cml_new(CmlVal val) {
    CmlObj* o = alloc_obj(TAG_HEAP_PTR);
    o->u.ptr.alive = 1;
    o->u.ptr.is_stack = 0;
    o->u.ptr.val = val;
    o->u.ptr.slot = NULL;
    ++g_heap_live;
    return o;
}

CmlVal cml_close(CmlVal ptr) {
    if (!ptr || ptr->tag != TAG_HEAP_PTR) rt_panic("close expects pointer");
    if (ptr->u.ptr.is_stack) rt_panic("cannot close stack pointer");
    if (!ptr->u.ptr.alive) rt_panic("double-close detected");
    ptr->u.ptr.alive = 0;
    ptr->u.ptr.val = NULL;
    --g_heap_live;
    return cml_unit();
}

static CmlVal ptr_read_inner(CmlVal ptr) {
    if (!ptr || ptr->tag != TAG_HEAP_PTR) rt_panic("dereference of non-pointer");
    if (!ptr->u.ptr.alive) rt_panic("use-after-free / dangling pointer access");
    return ptr->u.ptr.is_stack ? *(ptr->u.ptr.slot) : ptr->u.ptr.val;
}

CmlVal cml_deref(CmlVal ptr) { return ptr_read_inner(ptr); }

CmlVal cml_set_deref(CmlVal ptr, CmlVal val) {
    if (!ptr || ptr->tag != TAG_HEAP_PTR) rt_panic("deref assignment on non-pointer");
    if (!ptr->u.ptr.alive) rt_panic("use-after-free on deref write");
    if (ptr->u.ptr.is_stack) *(ptr->u.ptr.slot) = val;
    else ptr->u.ptr.val = val;
    return cml_unit();
}

CmlVal cml_ptr_get(CmlVal ptr, const char* fld) {
    return cml_field_get(ptr_read_inner(ptr), fld);
}

CmlVal cml_ptr_set(CmlVal ptr, const char* fld, CmlVal val) {
    return cml_field_set(ptr_read_inner(ptr), fld, val);
}

CmlVal cml_addr_of_cell(CmlVal* cell) {
    CmlObj* o = alloc_obj(TAG_HEAP_PTR);
    o->u.ptr.alive = 1;
    o->u.ptr.is_stack = 1;
    o->u.ptr.slot = cell;
    return o;
}

void cml_invalidate_cell(CmlVal ptr) {
    if (ptr && ptr->tag == TAG_HEAP_PTR && ptr->u.ptr.is_stack) ptr->u.ptr.alive = 0;
}

/* Pattern Matching */
int64_t cml_match_int(CmlVal v, int64_t target) { return v && v->tag == TAG_INT && v->u.i == target; }
int64_t cml_match_float(CmlVal v, double target) { return v && v->tag == TAG_FLOAT && v->u.f == target; }
int64_t cml_match_bool(CmlVal v, int64_t target) { return v && v->tag == TAG_BOOL && v->u.i == target; }
int64_t cml_match_str(CmlVal v, const char* s, int64_t len) {
    return v && v->tag == TAG_STR && v->u.str.len == len &&
           (len == 0 || memcmp(v->u.str.data, s, (size_t)len) == 0);
}
int64_t cml_match_unit(CmlVal v) { return v && v->tag == TAG_UNIT; }
int64_t cml_match_tuple(CmlVal v, int64_t arity) { return v && v->tag == TAG_TUPLE && v->u.tuple.len == arity; }
int64_t cml_match_list_len(CmlVal v, int64_t len) {
    if (!v) return 0;
    if (len == 0) return v->tag == TAG_NIL;
    return v->tag == TAG_CONS && v->u.cons.len == len;
}
int64_t cml_match_cons(CmlVal v) { return v && v->tag == TAG_CONS; }
int64_t cml_match_adt(CmlVal v, int64_t tag) { return v && v->tag == TAG_ADT && v->u.adt.tag_id == tag; }

CmlVal cml_tuple_get(CmlVal v, int64_t idx) { return v->u.tuple.items[idx]; }
CmlVal cml_list_get(CmlVal v, int64_t idx) {
    CmlVal cur = v;
    for (int64_t i = 0; i < idx; ++i) cur = cur->u.cons.tail;
    return cur->u.cons.head;
}
CmlVal cml_list_tail(CmlVal v, int64_t drop) {
    CmlVal cur = v;
    for (int64_t i = 0; i < drop; ++i) cur = cur->u.cons.tail;
    return cur;
}
CmlVal cml_adt_field(CmlVal v, int64_t idx) { return v->u.adt.fields[idx]; }
void cml_match_fail(void) { rt_panic("non-exhaustive pattern match"); }

/* Builtins */
static CmlVal bi_print(CmlVal* env, CmlVal* a) {
    (void)env;
    int64_t len = 0;
    char* s = val_to_cstr(a[0], 0, &len);
    fwrite(s, 1, (size_t)len, stdout);
    free(s);
    return cml_unit();
}

static CmlVal bi_println(CmlVal* env, CmlVal* a) {
    (void)env;
    int64_t len = 0;
    char* s = val_to_cstr(a[0], 0, &len);
    fwrite(s, 1, (size_t)len, stdout);
    putchar('\n');
    free(s);
    return cml_unit();
}

static CmlVal bi_to_string(CmlVal* env, CmlVal* a) {
    (void)env;
    int64_t len = 0;
    char* s = val_to_cstr(a[0], 0, &len);
    CmlVal res = cml_str(s, len);
    free(s);
    return res;
}

static CmlVal bi_not(CmlVal* env, CmlVal* a) { (void)env; return cml_not(a[0]); }
static CmlVal bi_abs(CmlVal* env, CmlVal* a) {
    (void)env;
    int64_t v = a[0]->u.i;
    return cml_int(v < 0 ? -v : v);
}
static CmlVal bi_int_of_float(CmlVal* env, CmlVal* a) { (void)env; return cml_int((int64_t)a[0]->u.f); }
static CmlVal bi_float_of_int(CmlVal* env, CmlVal* a) { (void)env; return cml_float((double)a[0]->u.i); }

static CmlVal bi_int_of_string(CmlVal* env, CmlVal* a) {
    (void)env;
    char* end = NULL;
    long long v = strtoll(a[0]->u.str.data, &end, 10);
    if (end == a[0]->u.str.data || *end != '\0') rt_panic("int_of_string: invalid integer");
    return cml_int((int64_t)v);
}

static CmlVal bi_float_of_string(CmlVal* env, CmlVal* a) {
    (void)env;
    char* end = NULL;
    double v = strtod(a[0]->u.str.data, &end);
    if (end == a[0]->u.str.data || *end != '\0') rt_panic("float_of_string: invalid float");
    return cml_float(v);
}

static CmlVal bi_str_len(CmlVal* env, CmlVal* a) { (void)env; return cml_int(a[0]->u.str.len); }

static CmlVal bi_str_sub(CmlVal* env, CmlVal* a) {
    (void)env;
    int64_t slen = a[0]->u.str.len;
    int64_t start = a[1]->u.i;
    int64_t count = a[2]->u.i;
    if (start < 0 || count < 0 || start + count > slen) rt_panic("str_sub out of range");
    return cml_str(a[0]->u.str.data + start, count);
}

static CmlVal bi_str_byte_at(CmlVal* env, CmlVal* a) {
    (void)env;
    int64_t idx = a[1]->u.i;
    if (idx < 0 || idx >= a[0]->u.str.len) return cml_int(-1);
    return cml_int((unsigned char)a[0]->u.str.data[idx]);
}

static CmlVal bi_char_from_code(CmlVal* env, CmlVal* a) {
    (void)env;
    int64_t c = a[0]->u.i;
    if (c < 0 || c > 255) rt_panic("char_from_code out of range");
    char ch = (char)c;
    return cml_str(&ch, 1);
}

static CmlVal bi_str_concat_list(CmlVal* env, CmlVal* a) {
    (void)env;
    StrBuf sb = {NULL, 0, 0};
    sb_append(&sb, "", 0);
    CmlVal cur = a[0];
    while (cur->tag == TAG_CONS) {
        CmlVal s = cur->u.cons.head;
        sb_append(&sb, s->u.str.data, (size_t)s->u.str.len);
        cur = cur->u.cons.tail;
    }
    CmlVal res = cml_str(sb.buf, (int64_t)sb.len);
    free(sb.buf);
    return res;
}

static CmlVal bi_list_len(CmlVal* env, CmlVal* a) {
    (void)env;
    if (a[0]->tag == TAG_NIL) return cml_int(0);
    if (a[0]->tag == TAG_CONS) return cml_int(a[0]->u.cons.len);
    rt_panic("list_len expects List");
    return cml_int(0);
}

static CmlVal bi_is_empty(CmlVal* env, CmlVal* a) {
    (void)env;
    return cml_bool(a[0]->tag == TAG_NIL);
}

static CmlVal bi_head(CmlVal* env, CmlVal* a) {
    (void)env;
    if (a[0]->tag != TAG_CONS) rt_panic("head of empty list");
    return a[0]->u.cons.head;
}

static CmlVal bi_tail(CmlVal* env, CmlVal* a) {
    (void)env;
    if (a[0]->tag != TAG_CONS) rt_panic("tail of empty list");
    return a[0]->u.cons.tail;
}

static CmlVal bi_list_nth(CmlVal* env, CmlVal* a) {
    (void)env;
    int64_t idx = a[1]->u.i;
    CmlVal cur = a[0];
    if (idx < 0) rt_panic("list_nth negative index");
    while (idx > 0 && cur->tag == TAG_CONS) {
        cur = cur->u.cons.tail;
        --idx;
    }
    if (cur->tag != TAG_CONS) rt_panic("list_nth out of bounds");
    return cur->u.cons.head;
}

static CmlVal bi_list_rev(CmlVal* env, CmlVal* a) {
    (void)env;
    CmlVal acc = cml_nil();
    CmlVal cur = a[0];
    while (cur->tag == TAG_CONS) {
        acc = cml_cons(cur->u.cons.head, acc);
        cur = cur->u.cons.tail;
    }
    return acc;
}

static CmlVal bi_list_append(CmlVal* env, CmlVal* a) {
    (void)env;
    CmlVal rev_a = bi_list_rev(NULL, &a[0]);
    CmlVal acc = a[1];
    CmlVal cur = rev_a;
    while (cur->tag == TAG_CONS) {
        acc = cml_cons(cur->u.cons.head, acc);
        cur = cur->u.cons.tail;
    }
    return acc;
}

static CmlVal bi_list_snoc(CmlVal* env, CmlVal* a) {
    (void)env;
    CmlVal single[2] = {a[0], cml_cons(a[1], cml_nil())};
    return bi_list_append(NULL, single);
}

static CmlVal bi_panic(CmlVal* env, CmlVal* a) {
    (void)env;
    rt_panic(a[0]->u.str.data);
    return cml_unit();
}

static CmlVal bi_read_file(CmlVal* env, CmlVal* a) {
    (void)env;
    FILE* f = fopen(a[0]->u.str.data, "rb");
    if (!f) rt_panic("read_file: could not open file");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)(sz > 0 ? sz : 0) + 1);
    size_t nr = sz > 0 ? fread(buf, 1, (size_t)sz, f) : 0;
    fclose(f);
    buf[nr] = '\0';
    CmlVal res = cml_str(buf, (int64_t)nr);
    free(buf);
    return res;
}

static CmlVal bi_write_file(CmlVal* env, CmlVal* a) {
    (void)env;
    FILE* f = fopen(a[0]->u.str.data, "wb");
    if (!f) rt_panic("write_file: could not open file");
    if (a[1]->u.str.len > 0) {
        fwrite(a[1]->u.str.data, 1, (size_t)a[1]->u.str.len, f);
    }
    fclose(f);
    return cml_unit();
}

static CmlVal bi_file_exists(CmlVal* env, CmlVal* a) {
    (void)env;
    FILE* f = fopen(a[0]->u.str.data, "rb");
    if (f) { fclose(f); return cml_bool(1); }
    return cml_bool(0);
}

static CmlVal bi_cli_args(CmlVal* env, CmlVal* a) {
    (void)env; (void)a;
    CmlVal acc = cml_nil();
    for (int i = g_argc - 1; i >= 1; --i) {
        acc = cml_cons(cml_cstr(g_argv[i]), acc);
    }
    return acc;
}

static CmlVal bi_sys_exec(CmlVal* env, CmlVal* a) {
    (void)env;
    int rc = system(a[0]->u.str.data);
    return cml_int((int64_t)rc);
}

static CmlVal bi_heap_live_count(CmlVal* env, CmlVal* a) {
    (void)env; (void)a;
    return cml_int(g_heap_live);
}

static CmlVal bi_ptr_is_alive(CmlVal* env, CmlVal* a) {
    (void)env;
    return cml_bool(a[0] && a[0]->tag == TAG_HEAP_PTR && a[0]->u.ptr.alive);
}

static CmlVal bi_math_sqrt(CmlVal* env, CmlVal* a) { (void)env; return cml_float(sqrt(a[0]->u.f)); }
static CmlVal bi_math_sin(CmlVal* env, CmlVal* a) { (void)env; return cml_float(sin(a[0]->u.f)); }
static CmlVal bi_math_cos(CmlVal* env, CmlVal* a) { (void)env; return cml_float(cos(a[0]->u.f)); }
static CmlVal bi_math_floor(CmlVal* env, CmlVal* a) { (void)env; return cml_float(floor(a[0]->u.f)); }
static CmlVal bi_math_ceil(CmlVal* env, CmlVal* a) { (void)env; return cml_float(ceil(a[0]->u.f)); }

CmlVal cml_builtin_closure(const char* name) {
    if (strcmp(name, "print") == 0) return cml_closure(bi_print, 1, 0, NULL);
    if (strcmp(name, "println") == 0) return cml_closure(bi_println, 1, 0, NULL);
    if (strcmp(name, "to_string") == 0) return cml_closure(bi_to_string, 1, 0, NULL);
    if (strcmp(name, "not") == 0) return cml_closure(bi_not, 1, 0, NULL);
    if (strcmp(name, "abs") == 0) return cml_closure(bi_abs, 1, 0, NULL);
    if (strcmp(name, "int_of_float") == 0) return cml_closure(bi_int_of_float, 1, 0, NULL);
    if (strcmp(name, "float_of_int") == 0) return cml_closure(bi_float_of_int, 1, 0, NULL);
    if (strcmp(name, "int_of_string") == 0) return cml_closure(bi_int_of_string, 1, 0, NULL);
    if (strcmp(name, "float_of_string") == 0) return cml_closure(bi_float_of_string, 1, 0, NULL);
    if (strcmp(name, "str_len") == 0) return cml_closure(bi_str_len, 1, 0, NULL);
    if (strcmp(name, "str_sub") == 0) return cml_closure(bi_str_sub, 3, 0, NULL);
    if (strcmp(name, "str_byte_at") == 0) return cml_closure(bi_str_byte_at, 2, 0, NULL);
    if (strcmp(name, "char_from_code") == 0) return cml_closure(bi_char_from_code, 1, 0, NULL);
    if (strcmp(name, "str_concat_list") == 0) return cml_closure(bi_str_concat_list, 1, 0, NULL);
    if (strcmp(name, "list_len") == 0) return cml_closure(bi_list_len, 1, 0, NULL);
    if (strcmp(name, "is_empty") == 0) return cml_closure(bi_is_empty, 1, 0, NULL);
    if (strcmp(name, "head") == 0) return cml_closure(bi_head, 1, 0, NULL);
    if (strcmp(name, "tail") == 0) return cml_closure(bi_tail, 1, 0, NULL);
    if (strcmp(name, "list_nth") == 0) return cml_closure(bi_list_nth, 2, 0, NULL);
    if (strcmp(name, "list_rev") == 0) return cml_closure(bi_list_rev, 1, 0, NULL);
    if (strcmp(name, "list_append") == 0) return cml_closure(bi_list_append, 2, 0, NULL);
    if (strcmp(name, "list_snoc") == 0) return cml_closure(bi_list_snoc, 2, 0, NULL);
    if (strcmp(name, "panic") == 0) return cml_closure(bi_panic, 1, 0, NULL);
    if (strcmp(name, "read_file") == 0) return cml_closure(bi_read_file, 1, 0, NULL);
    if (strcmp(name, "write_file") == 0) return cml_closure(bi_write_file, 2, 0, NULL);
    if (strcmp(name, "file_exists") == 0) return cml_closure(bi_file_exists, 1, 0, NULL);
    if (strcmp(name, "cli_args") == 0) return cml_closure(bi_cli_args, 1, 0, NULL);
    if (strcmp(name, "sys_exec") == 0) return cml_closure(bi_sys_exec, 1, 0, NULL);
    if (strcmp(name, "heap_live_count") == 0) return cml_closure(bi_heap_live_count, 1, 0, NULL);
    if (strcmp(name, "ptr_is_alive") == 0) return cml_closure(bi_ptr_is_alive, 1, 0, NULL);
    if (strcmp(name, "sqrt") == 0) return cml_closure(bi_math_sqrt, 1, 0, NULL);
    if (strcmp(name, "sin") == 0) return cml_closure(bi_math_sin, 1, 0, NULL);
    if (strcmp(name, "cos") == 0) return cml_closure(bi_math_cos, 1, 0, NULL);
    if (strcmp(name, "floor") == 0) return cml_closure(bi_math_floor, 1, 0, NULL);
    if (strcmp(name, "ceil") == 0) return cml_closure(bi_math_ceil, 1, 0, NULL);
    fprintf(stderr, "runtime error: unknown builtin '%s'\n", name);
    exit(1);
}
