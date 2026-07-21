/* json.c - see json.h */
#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

/* ====================================================================== */
/* Parser                                                                 */
/* ====================================================================== */

typedef struct {
    const char *p;
    const char *err;
} P;

static void skip_ws(P *s) {
    while (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r')
        s->p++;
}

static JsonValue *value(P *s);

static JsonValue *new_val(JsonType t) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (v) v->type = t;
    return v;
}

static char *parse_string_raw(P *s) {
    /* assumes *s->p == '"' */
    s->p++;
    size_t cap = 16, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) { s->err = "oom"; return NULL; }
    while (*s->p && *s->p != '"') {
        char c = *s->p++;
        if (c == '\\') {
            char e = *s->p++;
            switch (e) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    /* Basic BMP \uXXXX -> UTF-8. */
                    unsigned int cp = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = *s->p++;
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        else { s->err = "bad \\u escape"; free(out); return NULL; }
                    }
                    /* encode */
                    char tmp[4]; int tl = 0;
                    if (cp < 0x80) { tmp[tl++] = (char)cp; }
                    else if (cp < 0x800) {
                        tmp[tl++] = (char)(0xC0 | (cp >> 6));
                        tmp[tl++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        tmp[tl++] = (char)(0xE0 | (cp >> 12));
                        tmp[tl++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        tmp[tl++] = (char)(0x80 | (cp & 0x3F));
                    }
                    for (int i = 0; i < tl; i++) {
                        if (len + 1 >= cap) { cap *= 2; char *n = realloc(out, cap); if (!n){s->err="oom";free(out);return NULL;} out = n; }
                        out[len++] = tmp[i];
                    }
                    continue;
                }
                default: s->err = "bad escape"; free(out); return NULL;
            }
        }
        if (len + 1 >= cap) { cap *= 2; char *n = realloc(out, cap); if (!n){s->err="oom";free(out);return NULL;} out = n; }
        out[len++] = c;
    }
    if (*s->p != '"') { s->err = "unterminated string"; free(out); return NULL; }
    s->p++;
    out[len] = '\0';
    return out;
}

static JsonValue *parse_string(P *s) {
    char *str = parse_string_raw(s);
    if (!str) return NULL;
    JsonValue *v = new_val(JSON_STRING);
    if (!v) { free(str); s->err = "oom"; return NULL; }
    v->u.string = str;
    return v;
}

static JsonValue *parse_number(P *s) {
    char *end = NULL;
    double d = strtod(s->p, &end);
    if (end == s->p) { s->err = "bad number"; return NULL; }
    s->p = end;
    JsonValue *v = new_val(JSON_NUMBER);
    if (!v) { s->err = "oom"; return NULL; }
    v->u.number = d;
    return v;
}

static JsonValue *parse_array(P *s) {
    s->p++; /* [ */
    JsonValue *v = new_val(JSON_ARRAY);
    if (!v) { s->err = "oom"; return NULL; }
    size_t cap = 0;
    skip_ws(s);
    if (*s->p == ']') { s->p++; return v; }
    for (;;) {
        JsonValue *item = value(s);
        if (!item) { json_free(v); return NULL; }
        if (v->u.array.count == cap) {
            cap = cap ? cap * 2 : 4;
            JsonValue **n = realloc(v->u.array.items, cap * sizeof(*n));
            if (!n) { s->err = "oom"; json_free(item); json_free(v); return NULL; }
            v->u.array.items = n;
        }
        v->u.array.items[v->u.array.count++] = item;
        skip_ws(s);
        if (*s->p == ',') { s->p++; skip_ws(s); continue; }
        if (*s->p == ']') { s->p++; break; }
        s->err = "expected , or ]"; json_free(v); return NULL;
    }
    return v;
}

static JsonValue *parse_object(P *s) {
    s->p++; /* { */
    JsonValue *v = new_val(JSON_OBJECT);
    if (!v) { s->err = "oom"; return NULL; }
    size_t cap = 0;
    skip_ws(s);
    if (*s->p == '}') { s->p++; return v; }
    for (;;) {
        skip_ws(s);
        if (*s->p != '"') { s->err = "expected key string"; json_free(v); return NULL; }
        char *key = parse_string_raw(s);
        if (!key) { json_free(v); return NULL; }
        skip_ws(s);
        if (*s->p != ':') { s->err = "expected :"; free(key); json_free(v); return NULL; }
        s->p++;
        JsonValue *val = value(s);
        if (!val) { free(key); json_free(v); return NULL; }
        if (v->u.object.count == cap) {
            cap = cap ? cap * 2 : 4;
            char **nk = realloc(v->u.object.keys, cap * sizeof(*nk));
            JsonValue **nv = realloc(v->u.object.vals, cap * sizeof(*nv));
            if (!nk || !nv) { s->err = "oom"; free(key); json_free(val); json_free(v);
                              free(nk); free(nv); return NULL; }
            v->u.object.keys = nk; v->u.object.vals = nv;
        }
        v->u.object.keys[v->u.object.count] = key;
        v->u.object.vals[v->u.object.count] = val;
        v->u.object.count++;
        skip_ws(s);
        if (*s->p == ',') { s->p++; continue; }
        if (*s->p == '}') { s->p++; break; }
        s->err = "expected , or }"; json_free(v); return NULL;
    }
    return v;
}

static JsonValue *value(P *s) {
    skip_ws(s);
    char c = *s->p;
    switch (c) {
        case '{': return parse_object(s);
        case '[': return parse_array(s);
        case '"': return parse_string(s);
        case 't':
            if (strncmp(s->p, "true", 4) == 0) { s->p += 4; JsonValue *v = new_val(JSON_BOOL); v->u.boolean = 1; return v; }
            s->err = "bad literal"; return NULL;
        case 'f':
            if (strncmp(s->p, "false", 5) == 0) { s->p += 5; JsonValue *v = new_val(JSON_BOOL); v->u.boolean = 0; return v; }
            s->err = "bad literal"; return NULL;
        case 'n':
            if (strncmp(s->p, "null", 4) == 0) { s->p += 4; return new_val(JSON_NULL); }
            s->err = "bad literal"; return NULL;
        default:
            if (c == '-' || (c >= '0' && c <= '9')) return parse_number(s);
            s->err = "unexpected character"; return NULL;
    }
}

JsonValue *json_parse(const char *text, const char **err) {
    /* Skip a UTF-8 BOM if present. */
    if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF) text += 3;
    P s = { text, NULL };
    JsonValue *v = value(&s);
    if (!v) { if (err) *err = s.err ? s.err : "parse error"; return NULL; }
    skip_ws(&s);
    if (*s.p != '\0') { if (err) *err = "trailing garbage"; json_free(v); return NULL; }
    return v;
}

void json_free(JsonValue *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING: free(v->u.string); break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->u.array.count; i++) json_free(v->u.array.items[i]);
            free(v->u.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->u.object.count; i++) {
                free(v->u.object.keys[i]);
                json_free(v->u.object.vals[i]);
            }
            free(v->u.object.keys);
            free(v->u.object.vals);
            break;
        default: break;
    }
    free(v);
}

JsonValue *json_get(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < obj->u.object.count; i++)
        if (strcmp(obj->u.object.keys[i], key) == 0)
            return obj->u.object.vals[i];
    return NULL;
}

double json_get_number(const JsonValue *obj, const char *key, double dflt) {
    JsonValue *v = json_get(obj, key);
    if (v && v->type == JSON_NUMBER) return v->u.number;
    if (v && v->type == JSON_BOOL)   return v->u.boolean;
    return dflt;
}

int json_get_bool(const JsonValue *obj, const char *key, int dflt) {
    JsonValue *v = json_get(obj, key);
    if (v && v->type == JSON_BOOL)   return v->u.boolean;
    if (v && v->type == JSON_NUMBER) return v->u.number != 0.0;
    return dflt;
}

const char *json_get_string(const JsonValue *obj, const char *key, const char *dflt) {
    JsonValue *v = json_get(obj, key);
    if (v && v->type == JSON_STRING) return v->u.string;
    return dflt;
}

/* ====================================================================== */
/* Writer                                                                 */
/* ====================================================================== */

static void w_ensure(JsonWriter *w, size_t extra) {
    if (w->err) return;
    if (w->len + extra + 1 > w->cap) {
        size_t nc = w->cap ? w->cap : 256;
        while (nc < w->len + extra + 1) nc *= 2;
        char *n = realloc(w->buf, nc);
        if (!n) { w->err = 1; return; }
        w->buf = n; w->cap = nc;
    }
}

static void w_raw(JsonWriter *w, const char *s, size_t n) {
    w_ensure(w, n);
    if (w->err) return;
    memcpy(w->buf + w->len, s, n);
    w->len += n;
    w->buf[w->len] = '\0';
}

static void w_char(JsonWriter *w, char c) { w_raw(w, &c, 1); }

void json_writer_init(JsonWriter *w, int pretty) {
    memset(w, 0, sizeof(*w));
    w->pretty = pretty;
}
void json_writer_free(JsonWriter *w) { free(w->buf); memset(w, 0, sizeof(*w)); }
char *json_writer_take(JsonWriter *w) { char *b = w->buf; w->buf = NULL; w->cap = w->len = 0; return b; }

static void w_indent(JsonWriter *w) {
    if (!w->pretty) return;
    w_char(w, '\n');
    for (int i = 0; i < w->depth; i++) w_raw(w, "  ", 2);
}

/* Emit the separator (comma + indent) before a value token, unless this value
 * directly follows a key. Marks the current container as no longer empty. */
static void w_pre_value(JsonWriter *w) {
    if (w->after_key) { w->after_key = 0; return; }
    if (w->depth > 0) {
        unsigned long long bit = 1ULL << (w->depth - 1);
        if (!(w->first & bit)) w_char(w, ',');   /* not the first element */
        w_indent(w);
        w->first &= ~bit;                          /* first consumed */
    }
}

void json_str(JsonWriter *w, const char *s) {
    w_pre_value(w);
    w_char(w, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"':  w_raw(w, "\\\"", 2); break;
            case '\\': w_raw(w, "\\\\", 2); break;
            case '\n': w_raw(w, "\\n", 2); break;
            case '\t': w_raw(w, "\\t", 2); break;
            case '\r': w_raw(w, "\\r", 2); break;
            case '\b': w_raw(w, "\\b", 2); break;
            case '\f': w_raw(w, "\\f", 2); break;
            default:
                if (*p < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", *p); w_raw(w, b, 6); }
                else w_char(w, (char)*p);
        }
    }
    w_char(w, '"');
}

void json_num(JsonWriter *w, double x) {
    w_pre_value(w);
    char b[40];
    if (isnan(x) || isinf(x)) { w_raw(w, "null", 4); return; } /* JSON has no NaN/Inf */
    /* compact but faithful */
    snprintf(b, sizeof b, "%.10g", x);
    w_raw(w, b, strlen(b));
}

void json_int(JsonWriter *w, long long x) {
    w_pre_value(w);
    char b[32];
    snprintf(b, sizeof b, "%lld", x);
    w_raw(w, b, strlen(b));
}

void json_bool(JsonWriter *w, int b) {
    w_pre_value(w);
    if (b) w_raw(w, "true", 4); else w_raw(w, "false", 5);
}

void json_null(JsonWriter *w) { w_pre_value(w); w_raw(w, "null", 4); }

void json_key(JsonWriter *w, const char *key) {
    /* A key occupies an element slot (separator + indent), then emits
     * "key": and flags the following value to skip its own separator. */
    w_pre_value(w);
    w_char(w, '"');
    for (const char *p = key; *p; p++) {
        if (*p == '"' || *p == '\\') w_char(w, '\\');
        w_char(w, *p);
    }
    w_char(w, '"');
    w_raw(w, w->pretty ? ": " : ":", w->pretty ? 2 : 1);
    w->after_key = 1;
}

void json_obj_begin(JsonWriter *w) {
    w_pre_value(w);
    w_char(w, '{');
    w->depth++;
    w->first |= (1ULL << (w->depth - 1));   /* new container starts empty */
}
void json_obj_end(JsonWriter *w) {
    int empty = (w->first & (1ULL << (w->depth - 1))) != 0;
    w->depth--;
    if (!empty) w_indent(w);
    w_char(w, '}');
}
void json_arr_begin(JsonWriter *w) {
    w_pre_value(w);
    w_char(w, '[');
    w->depth++;
    w->first |= (1ULL << (w->depth - 1));
}
void json_arr_end(JsonWriter *w) {
    int empty = (w->first & (1ULL << (w->depth - 1))) != 0;
    w->depth--;
    if (!empty) w_indent(w);
    w_char(w, ']');
}

void json_kv_str(JsonWriter *w, const char *k, const char *s) { json_key(w, k); json_str(w, s); }
void json_kv_num(JsonWriter *w, const char *k, double x)      { json_key(w, k); json_num(w, x); }
void json_kv_int(JsonWriter *w, const char *k, long long x)   { json_key(w, k); json_int(w, x); }
void json_kv_bool(JsonWriter *w, const char *k, int b)        { json_key(w, k); json_bool(w, b); }
