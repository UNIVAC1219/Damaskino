/* catalog.c - see catalog.h */
#include "catalog.h"
#include "scenario.h"   /* dmk_read_file */
#include "json.h"
#include <stdlib.h>
#include <string.h>
#include "dmk_portable.h"
#include <stdio.h>

static const char *find_existing(const char *const *paths, int n) {
    for (int i = 0; i < n; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (f) { fclose(f); return paths[i]; }
    }
    return NULL;
}

const char *dmk_default_targets_path(void) {
    static char buf[512];
    const char *env = getenv("DMK_DATA_DIR");
    if (env) { snprintf(buf, sizeof buf, "%s/targets.json", env);
               FILE *f = fopen(buf, "rb"); if (f) { fclose(f); return buf; } }
    const char *cands[] = { "data/targets.json", "./targets.json", "../data/targets.json" };
    const char *p = find_existing(cands, 3);
    return p ? p : "data/targets.json";
}

const char *dmk_default_weapons_path(void) {
    static char buf[512];
    const char *env = getenv("DMK_DATA_DIR");
    if (env) { snprintf(buf, sizeof buf, "%s/weapons.json", env);
               FILE *f = fopen(buf, "rb"); if (f) { fclose(f); return buf; } }
    const char *cands[] = { "data/weapons.json", "./weapons.json", "../data/weapons.json" };
    const char *p = find_existing(cands, 3);
    return p ? p : "data/weapons.json";
}

static void copy_str(char *dst, size_t cap, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

int dmk_load_targets(const char *path, DmkTarget **out, int *count) {
    *out = NULL; *count = 0;
    char *text = dmk_read_file(path);
    if (!text) return -1;
    const char *err = NULL;
    JsonValue *root = json_parse(text, &err);
    free(text);
    if (!root) return -2;
    JsonValue *arr = json_get(root, "targets");
    if (!arr || arr->type != JSON_ARRAY) { json_free(root); return -3; }

    int n = (int)arr->u.array.count;
    DmkTarget *t = (DmkTarget *)calloc(n, sizeof(DmkTarget));
    if (!t) { json_free(root); return -4; }
    for (int i = 0; i < n; i++) {
        JsonValue *o = arr->u.array.items[i];
        t[i].id = (int)json_get_number(o, "id", i + 1);
        copy_str(t[i].name, sizeof t[i].name, json_get_string(o, "name", ""));
        copy_str(t[i].state, sizeof t[i].state, json_get_string(o, "state", ""));
        copy_str(t[i].category, sizeof t[i].category, json_get_string(o, "category", ""));
        t[i].lat = json_get_number(o, "lat", 0.0);
        t[i].lon = json_get_number(o, "lon", 0.0);
        t[i].yield_kt = json_get_number(o, "yield_kt", 0.0);
        const char *burst = json_get_string(o, "burst", "surface");
        t[i].is_surface = (strcasecmp(burst, "air") != 0);
    }
    json_free(root);
    *out = t; *count = n;
    return 0;
}

int dmk_load_weapons(const char *path, DmkWeaponPreset **out, int *count) {
    *out = NULL; *count = 0;
    char *text = dmk_read_file(path);
    if (!text) return -1;
    const char *err = NULL;
    JsonValue *root = json_parse(text, &err);
    free(text);
    if (!root) return -2;
    JsonValue *arr = json_get(root, "weapons");
    if (!arr || arr->type != JSON_ARRAY) { json_free(root); return -3; }

    int n = (int)arr->u.array.count;
    DmkWeaponPreset *w = (DmkWeaponPreset *)calloc(n, sizeof(DmkWeaponPreset));
    if (!w) { json_free(root); return -4; }
    for (int i = 0; i < n; i++) {
        JsonValue *o = arr->u.array.items[i];
        copy_str(w[i].id, sizeof w[i].id, json_get_string(o, "id", ""));
        copy_str(w[i].name, sizeof w[i].name, json_get_string(o, "name", ""));
        w[i].yield_kt = json_get_number(o, "yield_kt", 0.0);
        w[i].fission_fraction = json_get_number(o, "fission_fraction", 0.5);
        const char *burst = json_get_string(o, "default_burst", "air");
        w[i].is_surface = (strcasecmp(burst, "air") != 0);
        w[i].hob_m = json_get_number(o, "hob_m", 0.0);
    }
    json_free(root);
    *out = w; *count = n;
    return 0;
}

int dmk_find_target_by_id(const DmkTarget *t, int n, int id) {
    for (int i = 0; i < n; i++) if (t[i].id == id) return i;
    return -1;
}
int dmk_find_target_by_name(const DmkTarget *t, int n, const char *needle) {
    for (int i = 0; i < n; i++) {
        /* case-insensitive substring */
        const char *hay = t[i].name;
        size_t hl = strlen(hay), nl = strlen(needle);
        if (nl == 0) continue;
        for (size_t j = 0; j + nl <= hl; j++)
            if (strncasecmp(hay + j, needle, nl) == 0) return i;
    }
    return -1;
}
int dmk_find_weapon_by_id(const DmkWeaponPreset *w, int n, const char *id) {
    for (int i = 0; i < n; i++) if (strcasecmp(w[i].id, id) == 0) return i;
    return -1;
}
