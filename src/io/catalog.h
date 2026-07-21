/*
 * catalog.h - Load the target aimpoint list (data/targets.json) and weapon
 * presets (data/weapons.json) produced by tools/build_targets.py.
 */
#ifndef DMK_CATALOG_H
#define DMK_CATALOG_H
#include "damaskino.h"

typedef struct {
    int    id;
    char   name[96];
    char   state[48];
    char   category[32];
    double lat, lon;
    double yield_kt;   /* preset yield from the source list */
    int    is_surface; /* preset burst type */
} DmkTarget;

typedef struct {
    char   id[32];
    char   name[96];
    double yield_kt;
    double fission_fraction;
    int    is_surface;
    double hob_m;
} DmkWeaponPreset;

/* Load arrays (caller frees with free()). Return 0 on success. */
int dmk_load_targets(const char *path, DmkTarget **out, int *count);
int dmk_load_weapons(const char *path, DmkWeaponPreset **out, int *count);

/* Resolve default data paths (checks $DMK_DATA_DIR, ./data, .). */
const char *dmk_default_targets_path(void);
const char *dmk_default_weapons_path(void);

/* Lookups. target by numeric id or case-insensitive name substring; weapon by
 * id. Return index or -1. */
int dmk_find_target_by_id(const DmkTarget *t, int n, int id);
int dmk_find_target_by_name(const DmkTarget *t, int n, const char *needle);
int dmk_find_weapon_by_id(const DmkWeaponPreset *w, int n, const char *id);

#endif
