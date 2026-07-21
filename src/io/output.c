/*
 * output.c - Result serialization: structured JSON, GeoJSON, and a
 * teletype-style human report.
 */
#include "output.h"
#include "geo.h"
#include <math.h>
#include <stdio.h>

/* ---- Shared analysis helpers ------------------------------------------ */

static real_t cell_dist_km(const DmkModel *m, int x, int y) {
    real_t dx = (x - m->gz_x) * m->grid.cell_km;
    real_t dy = (y - m->gz_y) * m->grid.cell_km;
    return sqrt(dx * dx + dy * dy);
}

/* ---- Structured JSON result ------------------------------------------- */
void dmk_write_result_json(const DmkModel *m, JsonWriter *w) {
    const DmkScenario *sc = &m->scenario;

    json_obj_begin(w);

    json_kv_str(w, "tool", "damaskino");
    json_kv_str(w, "version", DMK_VERSION_STRING);
    json_kv_str(w, "model_versions", m->model_versions);

    /* scenario echo */
    json_key(w, "scenario"); json_obj_begin(w);
        json_kv_str(w, "location", sc->gz.name);
        json_kv_num(w, "lat", sc->gz.lat);
        json_kv_num(w, "lon", sc->gz.lon);
        json_kv_num(w, "yield_kt", sc->weapon.yield_kt);
        json_kv_str(w, "burst", sc->weapon.is_surface_burst ? "surface" : "air");
        json_kv_num(w, "hob_m", sc->weapon.hob_m);
        json_kv_num(w, "fission_fraction", sc->weapon.fission_fraction);
        json_kv_num(w, "ref_time_hr", sc->cfg.ref_time_hr);
        json_key(w, "grid"); json_obj_begin(w);
            json_kv_int(w, "n", sc->cfg.grid_n);
            json_kv_num(w, "cell_km", sc->cfg.cell_km);
            json_kv_num(w, "span_km", sc->cfg.grid_n * sc->cfg.cell_km);
            json_kv_num(w, "max_range_km", sc->cfg.max_range_km);
        json_obj_end(w);
    json_obj_end(w);

    /* cloud */
    json_key(w, "cloud"); json_obj_begin(w);
        json_kv_num(w, "top_km", m->cloud.cloud_top_km);
        json_kv_num(w, "base_km", m->cloud.cloud_base_km);
        json_kv_num(w, "stem_diameter_km", m->cloud.stem_diameter_km);
        json_kv_num(w, "stabilization_min", m->cloud.stabilization_min);
    json_obj_end(w);

    /* particle distribution */
    json_key(w, "particles"); json_obj_begin(w);
        json_kv_num(w, "median_microns", m->particles.median_microns);
        json_kv_num(w, "geometric_sigma", m->particles.geometric_sigma);
        json_key(w, "mass_fraction"); json_arr_begin(w);
            for (int i = 0; i < DMK_NUM_PARTICLE_CLASSES; i++) {
                json_obj_begin(w);
                json_kv_num(w, "diameter_um", DMK_PARTICLE_DIAMETERS[i]);
                json_kv_num(w, "fraction", m->particles.mass_fraction[i]);
                json_obj_end(w);
            }
        json_arr_end(w);
    json_obj_end(w);

    /* contours: extent & area per dose level */
    static const real_t levels[] = {1, 10, 50, 100, 500, 1000, 5000};
    json_key(w, "contours"); json_arr_begin(w);
    for (unsigned li = 0; li < sizeof(levels)/sizeof(levels[0]); li++) {
        real_t level = levels[li];
        real_t max_ext = 0.0; long count = 0;
        for (int y = 0; y < m->grid.n; y++)
            for (int x = 0; x < m->grid.n; x++)
                if (m->grid.cell[(size_t)y*m->grid.n + x].dose_rate_rhr >= level) {
                    count++;
                    real_t d = cell_dist_km(m, x, y);
                    if (d > max_ext) max_ext = d;
                }
        json_obj_begin(w);
        json_kv_num(w, "level_rhr", level);
        json_kv_num(w, "max_extent_km", max_ext);
        json_kv_num(w, "area_km2", count * m->grid.cell_km * m->grid.cell_km);
        json_obj_end(w);
    }
    json_arr_end(w);

    json_obj_end(w);
}

/* ---- GeoJSON ---------------------------------------------------------- */
void dmk_write_geojson(const DmkModel *m, JsonWriter *w, real_t threshold) {
    const DmkScenario *sc = &m->scenario;
    real_t half = 0.5 * m->grid.cell_km;

    json_obj_begin(w);
    json_kv_str(w, "type", "FeatureCollection");
    json_key(w, "features"); json_arr_begin(w);

    /* Ground zero marker */
    json_obj_begin(w);
        json_kv_str(w, "type", "Feature");
        json_key(w, "properties"); json_obj_begin(w);
            json_kv_str(w, "kind", "ground_zero");
            json_kv_str(w, "name", sc->gz.name);
            json_kv_num(w, "yield_kt", sc->weapon.yield_kt);
        json_obj_end(w);
        json_key(w, "geometry"); json_obj_begin(w);
            json_kv_str(w, "type", "Point");
            json_key(w, "coordinates"); json_arr_begin(w);
                json_num(w, sc->gz.lon); json_num(w, sc->gz.lat);
            json_arr_end(w);
        json_obj_end(w);
    json_obj_end(w);

    /* Dose cells above threshold, as small squares */
    for (int y = 0; y < m->grid.n; y++) {
        for (int x = 0; x < m->grid.n; x++) {
            real_t dose = m->grid.cell[(size_t)y*m->grid.n + x].dose_rate_rhr;
            if (dose < threshold) continue;
            real_t east = (x - m->gz_x) * m->grid.cell_km;
            real_t north = (y - m->gz_y) * m->grid.cell_km;
            real_t lat[4], lon[4];
            dmk_offset_to_latlon(sc->gz.lat, sc->gz.lon, east-half, north-half, &lat[0], &lon[0]);
            dmk_offset_to_latlon(sc->gz.lat, sc->gz.lon, east+half, north-half, &lat[1], &lon[1]);
            dmk_offset_to_latlon(sc->gz.lat, sc->gz.lon, east+half, north+half, &lat[2], &lon[2]);
            dmk_offset_to_latlon(sc->gz.lat, sc->gz.lon, east-half, north+half, &lat[3], &lon[3]);

            json_obj_begin(w);
            json_kv_str(w, "type", "Feature");
            json_key(w, "properties"); json_obj_begin(w);
                json_kv_num(w, "dose_rate_rhr", dose);
                json_kv_num(w, "arrival_hr", m->grid.cell[(size_t)y*m->grid.n + x].arrival_hr);
            json_obj_end(w);
            json_key(w, "geometry"); json_obj_begin(w);
                json_kv_str(w, "type", "Polygon");
                json_key(w, "coordinates"); json_arr_begin(w);
                    json_arr_begin(w);
                    for (int k = 0; k < 4; k++) {
                        json_arr_begin(w); json_num(w, lon[k]); json_num(w, lat[k]); json_arr_end(w);
                    }
                    json_arr_begin(w); json_num(w, lon[0]); json_num(w, lat[0]); json_arr_end(w);
                    json_arr_end(w);
                json_arr_end(w);
            json_obj_end(w);
            json_obj_end(w);
        }
    }

    json_arr_end(w);
    json_obj_end(w);
}
