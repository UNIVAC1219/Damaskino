/*
 * json.h - Small, dependency-free JSON parser and writer.
 *
 * Recursive-descent parser producing a value tree; a streaming writer for
 * output. Sufficient for scenario input and JSON/GeoJSON output. Not compiled
 * into the UNIVAC-lite profile (which uses the interactive teletype path).
 */
#ifndef DMK_JSON_H
#define DMK_JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

struct JsonValue {
    JsonType type;
    union {
        int          boolean;
        double       number;
        char        *string;      /* NUL-terminated, owned */
        struct {                  /* array */
            JsonValue **items;
            size_t      count;
        } array;
        struct {                  /* object */
            char      **keys;
            JsonValue **vals;
            size_t      count;
        } object;
    } u;
};

/* Parse a NUL-terminated JSON document. Returns NULL on error; if err is
 * non-NULL it receives a static human-readable message. */
JsonValue *json_parse(const char *text, const char **err);
void       json_free(JsonValue *v);

/* Typed object lookups (return NULL / default if absent or wrong type). */
JsonValue *json_get(const JsonValue *obj, const char *key);
double     json_get_number(const JsonValue *obj, const char *key, double dflt);
int        json_get_bool(const JsonValue *obj, const char *key, int dflt);
const char*json_get_string(const JsonValue *obj, const char *key, const char *dflt);

/* ---- Streaming writer ------------------------------------------------- */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    int    err;
    /* pretty-print state */
    int    depth;
    int    pretty;
    /* per-depth "is the next element the first in its container" (bit/depth) */
    unsigned long long first;
    /* set immediately after a key so the following value emits no separator */
    int    after_key;
} JsonWriter;

void json_writer_init(JsonWriter *w, int pretty);
void json_writer_free(JsonWriter *w);   /* frees buffer */
char *json_writer_take(JsonWriter *w);  /* transfer ownership of buffer */

void json_obj_begin(JsonWriter *w);
void json_obj_end(JsonWriter *w);
void json_arr_begin(JsonWriter *w);
void json_arr_end(JsonWriter *w);
void json_key(JsonWriter *w, const char *key);
void json_str(JsonWriter *w, const char *s);
void json_num(JsonWriter *w, double x);
void json_int(JsonWriter *w, long long x);
void json_bool(JsonWriter *w, int b);
void json_null(JsonWriter *w);
/* Convenience: key + value in object context. */
void json_kv_str(JsonWriter *w, const char *k, const char *s);
void json_kv_num(JsonWriter *w, const char *k, double x);
void json_kv_int(JsonWriter *w, const char *k, long long x);
void json_kv_bool(JsonWriter *w, const char *k, int b);

#endif /* DMK_JSON_H */
