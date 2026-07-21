/* weather.c - see weather.h */
#include "weather.h"
#include "scenario.h"   /* dmk_read_file */
#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

int dmk_weather_ready(const DmkWindColumn *w) { return w && w->loaded && w->nlev > 0; }

static int alloc_levels(DmkWindColumn *w, int n) {
    w->alt_m = (real_t *)calloc(n, sizeof(real_t));
    w->u_ms  = (real_t *)calloc(n, sizeof(real_t));
    w->v_ms  = (real_t *)calloc(n, sizeof(real_t));
    if (!w->alt_m || !w->u_ms || !w->v_ms) return -1;
    w->nlev = n;
    return 0;
}

/* insertion sort levels by ascending altitude (small n) */
static void sort_levels(DmkWindColumn *w) {
    for (int i = 1; i < w->nlev; i++) {
        real_t a = w->alt_m[i], u = w->u_ms[i], v = w->v_ms[i];
        int j = i - 1;
        while (j >= 0 && w->alt_m[j] > a) {
            w->alt_m[j+1] = w->alt_m[j]; w->u_ms[j+1] = w->u_ms[j]; w->v_ms[j+1] = w->v_ms[j];
            j--;
        }
        w->alt_m[j+1] = a; w->u_ms[j+1] = u; w->v_ms[j+1] = v;
    }
}

int dmk_weather_load(const char *path, DmkWindColumn *out) {
    memset(out, 0, sizeof(*out));
    char *text = dmk_read_file(path);
    if (!text) return -1;
    const char *err = NULL;
    JsonValue *root = json_parse(text, &err);
    free(text);
    if (!root) return -2;

    const char *src = json_get_string(root, "source", "unknown");
    strncpy(out->source, src, sizeof(out->source) - 1);
    out->lat = json_get_number(root, "lat", 0.0);
    out->lon = json_get_number(root, "lon", 0.0);
    out->precip_mm_hr = json_get_number(root, "precip_mm_hr", 0.0);

    JsonValue *levels = json_get(root, "levels");
    if (!levels || levels->type != JSON_ARRAY || levels->u.array.count == 0) {
        json_free(root); return -3;
    }
    int n = (int)levels->u.array.count;
    if (alloc_levels(out, n) != 0) { json_free(root); dmk_weather_free(out); return -4; }
    for (int i = 0; i < n; i++) {
        JsonValue *L = levels->u.array.items[i];
        out->alt_m[i] = json_get_number(L, "altitude_m", 0.0);
        out->u_ms[i]  = json_get_number(L, "u_ms", 0.0);
        out->v_ms[i]  = json_get_number(L, "v_ms", 0.0);
    }
    json_free(root);
    sort_levels(out);
    out->loaded = 1;
    return 0;
}

void dmk_weather_free(DmkWindColumn *w) {
    if (!w) return;
    free(w->alt_m); free(w->u_ms); free(w->v_ms);
    w->alt_m = w->u_ms = w->v_ms = NULL;
    w->nlev = 0; w->loaded = 0;
}

void dmk_wind_at(const DmkWindColumn *w, real_t alt_m, real_t *u, real_t *v) {
    if (!dmk_weather_ready(w)) { *u = *v = 0.0; return; }
    if (alt_m <= w->alt_m[0])          { *u = w->u_ms[0]; *v = w->v_ms[0]; return; }
    if (alt_m >= w->alt_m[w->nlev-1])  { *u = w->u_ms[w->nlev-1]; *v = w->v_ms[w->nlev-1]; return; }
    for (int i = 1; i < w->nlev; i++) {
        if (alt_m <= w->alt_m[i]) {
            real_t t = (alt_m - w->alt_m[i-1]) / (w->alt_m[i] - w->alt_m[i-1]);
            *u = w->u_ms[i-1] + t * (w->u_ms[i] - w->u_ms[i-1]);
            *v = w->v_ms[i-1] + t * (w->v_ms[i] - w->v_ms[i-1]);
            return;
        }
    }
    *u = w->u_ms[w->nlev-1]; *v = w->v_ms[w->nlev-1];
}

void dmk_weather_from_atmosphere(const DmkAtmosphere *atm, DmkWindColumn *out) {
    memset(out, 0, sizeof(*out));
    strncpy(out->source, "hand-entered", sizeof(out->source) - 1);
    if (alloc_levels(out, atm->n_layers) != 0) return;
    for (int i = 0; i < atm->n_layers; i++) {
        out->alt_m[i] = atm->layer[i].altitude_ft * DMK_FT_TO_M;
        /* meteorological: wind FROM direction_deg -> blows TO reciprocal;
         * compass bearing b -> (u east = sin b, v north = cos b). */
        real_t spd = atm->layer[i].speed_kts * DMK_KNOTS_TO_MS;
        real_t bearing = (atm->layer[i].direction_deg + 180.0) * DMK_DEG_TO_RAD;
        out->u_ms[i] = spd * sin(bearing);
        out->v_ms[i] = spd * cos(bearing);
    }
    sort_levels(out);
    out->loaded = 1;
}
