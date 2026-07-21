/*
 * scenario.c - Parse a scenario JSON document into a DmkScenario.
 *
 * Schema (all fields optional; sensible defaults applied):
 * {
 *   "location": {"name": "...", "lat": 38.9, "lon": -77.0},
 *   "weapon":   {"yield_kt": 500, "burst": "surface"|"air",
 *                "hob_m": 0, "fission_fraction": 0.5},
 *   "wind": {
 *      "surface": {"speed_kts": 15, "direction_deg": 270},
 *      "layers": [ {"altitude_ft":0,"speed_kts":15,"direction_deg":270}, ... ]
 *   },
 *   "grid": {"n": 256, "cell_km": 1.0, "ref_time_hr": 1.0, "max_range_km": 0}
 * }
 */
#include "scenario.h"
#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <math.h>

char *dmk_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

int dmk_scenario_parse(const char *json_text, DmkScenario *out,
                       char *errbuf, int errlen) {
    dmk_scenario_defaults(out);

    const char *perr = NULL;
    JsonValue *root = json_parse(json_text, &perr);
    if (!root) {
        if (errbuf) snprintf(errbuf, errlen, "JSON parse error: %s", perr ? perr : "?");
        return 1;
    }
    if (root->type != JSON_OBJECT) {
        if (errbuf) snprintf(errbuf, errlen, "top-level value must be an object");
        json_free(root);
        return 1;
    }

    /* location */
    JsonValue *loc = json_get(root, "location");
    if (loc) {
        const char *name = json_get_string(loc, "name", NULL);
        if (name) { strncpy(out->gz.name, name, sizeof(out->gz.name) - 1);
                    out->gz.name[sizeof(out->gz.name)-1] = '\0'; }
        out->gz.lat = json_get_number(loc, "lat", out->gz.lat);
        out->gz.lon = json_get_number(loc, "lon", out->gz.lon);
    }

    /* weapon */
    JsonValue *wpn = json_get(root, "weapon");
    if (wpn) {
        out->weapon.yield_kt = json_get_number(wpn, "yield_kt", out->weapon.yield_kt);
        const char *burst = json_get_string(wpn, "burst", NULL);
        if (burst) {
            if (strcasecmp(burst, "air") == 0) out->weapon.is_surface_burst = 0;
            else out->weapon.is_surface_burst = 1;
        }
        out->weapon.hob_m = json_get_number(wpn, "hob_m", out->weapon.hob_m);
        out->weapon.fission_fraction =
            json_get_number(wpn, "fission_fraction", out->weapon.fission_fraction);

        /* Air burst with unspecified HOB: optimal-blast height. */
        if (!out->weapon.is_surface_burst && out->weapon.hob_m <= 0.0)
            out->weapon.hob_m = 540.0 * pow(out->weapon.yield_kt / 1000.0, 0.4);
    }

    /* wind */
    JsonValue *wind = json_get(root, "wind");
    int have_full_profile = 0;
    if (wind) {
        JsonValue *sfc = json_get(wind, "surface");
        if (sfc) {
            out->atmosphere.layer[0].altitude_ft = 0.0;
            out->atmosphere.layer[0].speed_kts = json_get_number(sfc, "speed_kts", 0.0);
            out->atmosphere.layer[0].direction_deg = json_get_number(sfc, "direction_deg", 270.0);
        }
        JsonValue *layers = json_get(wind, "layers");
        if (layers && layers->type == JSON_ARRAY && layers->u.array.count > 0) {
            int n = (int)layers->u.array.count;
            if (n > DMK_NUM_WIND_LAYERS) n = DMK_NUM_WIND_LAYERS;
            for (int i = 0; i < n; i++) {
                JsonValue *L = layers->u.array.items[i];
                out->atmosphere.layer[i].altitude_ft = json_get_number(L, "altitude_ft", DMK_DEFAULT_WIND_ALTITUDES_FT[i]);
                out->atmosphere.layer[i].speed_kts = json_get_number(L, "speed_kts", 0.0);
                out->atmosphere.layer[i].direction_deg = json_get_number(L, "direction_deg", 270.0);
            }
            out->atmosphere.n_layers = n;
            have_full_profile = 1;
        }
    }
    if (!have_full_profile) {
        /* estimate winds aloft from the surface layer */
        dmk_atmosphere_estimate_aloft(&out->atmosphere);
    }

    /* grid */
    JsonValue *grid = json_get(root, "grid");
    if (grid) {
        out->cfg.grid_n = (int)json_get_number(grid, "n", out->cfg.grid_n);
        out->cfg.cell_km = json_get_number(grid, "cell_km", out->cfg.cell_km);
        out->cfg.ref_time_hr = json_get_number(grid, "ref_time_hr", out->cfg.ref_time_hr);
        out->cfg.max_range_km = json_get_number(grid, "max_range_km", out->cfg.max_range_km);
    }

    json_free(root);

    /* ---- Validation: reject inputs that would yield NaN/inf grids ------- */
    if (!(out->weapon.yield_kt > 0.0) || out->weapon.yield_kt > 1.0e7) {
        if (errbuf) snprintf(errbuf, errlen,
            "weapon.yield_kt must be in (0, 1e7]; got %g", (double)out->weapon.yield_kt);
        return 1;
    }
    if (!(out->cfg.ref_time_hr > 0.0)) {
        if (errbuf) snprintf(errbuf, errlen,
            "grid.ref_time_hr must be > 0 (Way-Wigner decay diverges at t=0); got %g",
            (double)out->cfg.ref_time_hr);
        return 1;
    }
    if (out->weapon.fission_fraction < 0.0 || out->weapon.fission_fraction > 1.0) {
        if (errbuf) snprintf(errbuf, errlen,
            "weapon.fission_fraction must be in [0,1]; got %g",
            (double)out->weapon.fission_fraction);
        return 1;
    }
    if (out->cfg.grid_n < 3 || out->cfg.grid_n > 8192) {
        if (errbuf) snprintf(errbuf, errlen,
            "grid.n must be in [3, 8192]; got %d", out->cfg.grid_n);
        return 1;
    }
    if (!(out->cfg.cell_km > 0.0)) {
        if (errbuf) snprintf(errbuf, errlen,
            "grid.cell_km must be > 0; got %g", (double)out->cfg.cell_km);
        return 1;
    }
    if (out->gz.lat < -90.0 || out->gz.lat > 90.0 ||
        out->gz.lon < -180.0 || out->gz.lon > 180.0) {
        if (errbuf) snprintf(errbuf, errlen,
            "location lat/lon out of range: (%g, %g)",
            (double)out->gz.lat, (double)out->gz.lon);
        return 1;
    }
    return 0;
}
