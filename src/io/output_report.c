/*
 * output_report.c - Comprehensive multi-effect report (full profile only).
 */
#include "output_report.h"
#include "effects.h"
#include "geo.h"
#include <math.h>
#include <stdio.h>

static real_t default_visibility(const DmkCasualtyOpts *o) {
    return (o && o->visibility_km > 0.0) ? o->visibility_km : 20.0;
}

static void write_rings(JsonWriter *w, const char *name, const char *unit,
                        const DmkEffectRing *rings, int n) {
    json_key(w, name); json_arr_begin(w);
    for (int i = 0; i < n; i++) {
        json_obj_begin(w);
        json_kv_num(w, unit, rings[i].value);
        json_kv_num(w, "radius_km", rings[i].range_km);
        json_kv_num(w, "area_km2", M_PI * rings[i].range_km * rings[i].range_km);
        json_kv_str(w, "label", rings[i].label);
        json_obj_end(w);
    }
    json_arr_end(w);
}

void dmk_write_report_json(const DmkModel *m, const DmkPopulation *pop,
                           const DmkCasualtyOpts *opts, const DmkCasualties *cas,
                           JsonWriter *w) {
    const DmkScenario *sc = &m->scenario;
    real_t vis = default_visibility(opts);

    json_obj_begin(w);
    json_kv_str(w, "tool", "damaskino");
    json_kv_str(w, "version", DMK_VERSION_STRING);
    json_kv_str(w, "model_versions", m->model_versions);
    json_kv_str(w, "disclaimer",
        "Consequence estimate for civil-defense/education. Analytic effect "
        "models; absolute magnitudes calibrated in validation mode.");

    json_key(w, "scenario"); json_obj_begin(w);
        json_kv_str(w, "location", sc->gz.name);
        json_kv_num(w, "lat", sc->gz.lat);
        json_kv_num(w, "lon", sc->gz.lon);
        json_kv_num(w, "yield_kt", sc->weapon.yield_kt);
        json_kv_str(w, "burst", sc->weapon.is_surface_burst ? "surface" : "air");
        json_kv_num(w, "hob_m", sc->weapon.hob_m);
        json_kv_num(w, "fission_fraction", sc->weapon.fission_fraction);
        json_kv_num(w, "visibility_km", vis);
    json_obj_end(w);

    /* Effect rings */
    DmkEffectRing br[8], tr[8], rr[8];
    int nb = dmk_blast_rings(sc, br, 8);
    int nt = dmk_thermal_rings(sc, vis, tr, 8);
    int nr = dmk_radiation_rings(sc, rr, 8);
    json_key(w, "effects"); json_obj_begin(w);
        write_rings(w, "blast", "overpressure_psi", br, nb);
        write_rings(w, "thermal", "fluence_cal_cm2", tr, nt);
        write_rings(w, "prompt_radiation", "dose_rem", rr, nr);

        /* Cratering (surface bursts) */
        DmkCrater cr = dmk_crater(sc->weapon.yield_kt, sc->weapon.is_surface_burst);
        if (cr.radius_m > 0.0) {
            json_key(w, "crater"); json_obj_begin(w);
                json_kv_num(w, "radius_m", cr.radius_m);
                json_kv_num(w, "depth_m", cr.depth_m);
            json_obj_end(w);
        }
        /* High-altitude EMP */
        real_t emp_fp = dmk_hemp_footprint_km(sc->weapon.hob_m);
        if (emp_fp > 0.0) {
            json_key(w, "hemp"); json_obj_begin(w);
                json_kv_num(w, "footprint_radius_km", emp_fp);
                json_kv_num(w, "peak_field_kv_m", dmk_hemp_peak_field_kvm());
            json_obj_end(w);
        }
        /* Neutron activation near GZ */
        json_key(w, "neutron_activation"); json_obj_begin(w);
            json_kv_num(w, "dose_h1_rhr_at_0.5km",
                dmk_activation_dose_h1_rhr(sc->weapon.yield_kt, sc->weapon.fission_fraction, 0.5));
            json_kv_num(w, "dose_h1_rhr_at_1km",
                dmk_activation_dose_h1_rhr(sc->weapon.yield_kt, sc->weapon.fission_fraction, 1.0));
        json_obj_end(w);
    json_obj_end(w);

    /* Fallout summary */
    json_key(w, "fallout"); json_obj_begin(w);
        json_kv_num(w, "cloud_top_km", m->cloud.cloud_top_km);
        json_kv_num(w, "off_grid_fraction", m->off_grid_fraction);
        json_kv_bool(w, "plume_clipped", m->off_grid_fraction > 0.01);
        static const real_t levels[] = {1, 10, 100, 1000};
        json_key(w, "contours"); json_arr_begin(w);
        for (unsigned li = 0; li < sizeof(levels)/sizeof(levels[0]); li++) {
            real_t level = levels[li], max_ext = 0.0; long count = 0; int edge = 0;
            for (int y = 0; y < m->grid.n; y++)
                for (int x = 0; x < m->grid.n; x++)
                    if (m->grid.cell[(size_t)y*m->grid.n + x].dose_rate_rhr >= level) {
                        count++;
                        real_t dx=(x-m->gz_x)*m->grid.cell_km, dy=(y-m->gz_y)*m->grid.cell_km;
                        real_t d = sqrt(dx*dx+dy*dy);
                        if (d > max_ext) max_ext = d;
                        if (x==0||y==0||x==m->grid.n-1||y==m->grid.n-1) edge = 1;
                    }
            json_obj_begin(w);
            json_kv_num(w, "level_rhr", level);
            json_kv_num(w, "max_extent_km", max_ext);
            json_kv_num(w, "area_km2", count * m->grid.cell_km * m->grid.cell_km);
            json_kv_bool(w, "grid_limited", edge);
            json_obj_end(w);
        }
        json_arr_end(w);
    json_obj_end(w);

    /* Protective actions: 48 h integrated (unsheltered) dose zones for
     * shelter/evacuation planning, plus fallout arrival timing. */
    {
        real_t exposure = (opts && opts->exposure_hours > 0.0) ? opts->exposure_hours : 48.0;
        json_key(w, "protective_actions"); json_obj_begin(w);
            json_kv_num(w, "dose_window_hours", exposure);
            json_kv_str(w, "basis", "unsheltered (PF=1) accumulated fallout gamma dose from arrival");
            static const real_t dlev[] = {50.0, 150.0, 450.0};   /* rem */
            static const char *dlabel[] = {
                "50 rem: shelter recommended",
                "150 rem: evacuation / radiation sickness onset",
                "450 rem: ~LD50 without treatment" };
            json_key(w, "dose_zones"); json_arr_begin(w);
            for (unsigned li = 0; li < 3; li++) {
                real_t maxext = 0.0; long cnt = 0; int edge = 0;
                for (int y = 0; y < m->grid.n; y++)
                    for (int x = 0; x < m->grid.n; x++) {
                        const DmkCell *c = &m->grid.cell[(size_t)y*m->grid.n + x];
                        real_t dose = dmk_fallout_dose_rem(c->dose_rate_rhr, c->arrival_hr, exposure, 1.0);
                        if (dose >= dlev[li]) {
                            cnt++;
                            real_t dx=(x-m->gz_x)*m->grid.cell_km, dy=(y-m->gz_y)*m->grid.cell_km;
                            real_t d=sqrt(dx*dx+dy*dy); if (d>maxext) maxext=d;
                            if (x==0||y==0||x==m->grid.n-1||y==m->grid.n-1) edge=1;
                        }
                    }
                json_obj_begin(w);
                json_kv_num(w, "dose_rem", dlev[li]);
                json_kv_num(w, "max_extent_km", maxext);
                json_kv_num(w, "area_km2", cnt * m->grid.cell_km * m->grid.cell_km);
                json_kv_str(w, "action", dlabel[li]);
                json_kv_bool(w, "grid_limited", edge);
                json_obj_end(w);
            }
            json_arr_end(w);
            /* Arrival-time window over significant cells (>=1 R/hr): informs
             * how long before fallout reaches the far edge of the pattern. */
            real_t amin = 1e9, amax = 0.0;
            for (int i = 0; i < m->grid.n*m->grid.n; i++) {
                const DmkCell *c = &m->grid.cell[i];
                if (c->dose_rate_rhr >= 1.0 && c->arrival_hr > 0.0) {
                    if (c->arrival_hr < amin) amin = c->arrival_hr;
                    if (c->arrival_hr > amax) amax = c->arrival_hr;
                }
            }
            if (amin < 1e9) {
                json_kv_num(w, "first_arrival_hr", amin);
                json_kv_num(w, "last_arrival_hr", amax);
            }
        json_obj_end(w);
    }

    /* Casualties */
    if (cas) {
        json_key(w, "casualties"); json_obj_begin(w);
            if (opts) {
                json_key(w, "assumptions"); json_obj_begin(w);
                    json_kv_num(w, "pf_fallout", opts->pf_fallout);
                    json_kv_num(w, "pf_prompt", opts->pf_prompt);
                    json_kv_num(w, "thermal_exposed_fraction", opts->thermal_exposed_frac);
                    json_kv_num(w, "exposure_hours", opts->exposure_hours);
                    if (pop) {
                        json_kv_str(w, "population_mode", pop->mode == 0 ? "uniform" : "raster");
                        if (pop->mode == 0) {
                            json_kv_num(w, "population_density_km2", pop->uniform_density);
                            json_kv_str(w, "population_caveat",
                                "Uniform density fills the entire domain (incl. water/rural). "
                                "Absolute totals are a per-km^2 sanity tool; use a population "
                                "raster (--pop-asc) for real counts.");
                        }
                    }
                json_obj_end(w);
            }
            json_kv_num(w, "population_in_domain", cas->population_in_domain);
            json_kv_num(w, "fatalities", cas->fatalities);
            json_kv_num(w, "injuries", cas->injuries);
            json_key(w, "fatal_by_cause"); json_obj_begin(w);
                json_kv_num(w, "blast", cas->fatal_blast);
                json_kv_num(w, "thermal", cas->fatal_thermal);
                json_kv_num(w, "prompt_radiation", cas->fatal_prompt);
                json_kv_num(w, "fallout", cas->fatal_fallout);
            json_obj_end(w);
        json_obj_end(w);
    }

    json_obj_end(w);
}

/* ---- GeoJSON: fallout cells + ring circles ---------------------------- */
static void ring_circle(JsonWriter *w, const DmkScenario *sc, real_t radius_km,
                        const char *kind, real_t value, const char *label) {
    json_obj_begin(w);
    json_kv_str(w, "type", "Feature");
    json_key(w, "properties"); json_obj_begin(w);
        json_kv_str(w, "kind", kind);
        json_kv_num(w, "value", value);
        json_kv_num(w, "radius_km", radius_km);
        if (label) json_kv_str(w, "label", label);
    json_obj_end(w);
    json_key(w, "geometry"); json_obj_begin(w);
        json_kv_str(w, "type", "Polygon");
        json_key(w, "coordinates"); json_arr_begin(w);
            json_arr_begin(w);
            const int NP = 64;
            for (int i = 0; i <= NP; i++) {
                real_t th = 2.0 * M_PI * i / NP;
                real_t east = radius_km * sin(th), north = radius_km * cos(th);
                real_t lat, lon;
                dmk_offset_to_latlon(sc->gz.lat, sc->gz.lon, east, north, &lat, &lon);
                json_arr_begin(w); json_num(w, lon); json_num(w, lat); json_arr_end(w);
            }
            json_arr_end(w);
        json_arr_end(w);
    json_obj_end(w);
    json_obj_end(w);
}

void dmk_write_report_geojson(const DmkModel *m, const DmkCasualtyOpts *opts,
                              real_t fallout_threshold, JsonWriter *w) {
    const DmkScenario *sc = &m->scenario;
    real_t vis = default_visibility(opts);
    real_t half = 0.5 * m->grid.cell_km;

    json_obj_begin(w);
    json_kv_str(w, "type", "FeatureCollection");
    json_key(w, "features"); json_arr_begin(w);

    /* GZ */
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

    /* Effect rings as circles */
    DmkEffectRing br[8], tr[8], rr[8];
    int nb = dmk_blast_rings(sc, br, 8);
    int nt = dmk_thermal_rings(sc, vis, tr, 8);
    int nr = dmk_radiation_rings(sc, rr, 8);
    for (int i = 0; i < nb; i++) ring_circle(w, sc, br[i].range_km, "blast", br[i].value, br[i].label);
    for (int i = 0; i < nt; i++) ring_circle(w, sc, tr[i].range_km, "thermal", tr[i].value, tr[i].label);
    for (int i = 0; i < nr; i++) ring_circle(w, sc, rr[i].range_km, "prompt_radiation", rr[i].value, rr[i].label);

    /* Fallout cells above threshold */
    for (int y = 0; y < m->grid.n; y++) {
        for (int x = 0; x < m->grid.n; x++) {
            real_t dose = m->grid.cell[(size_t)y*m->grid.n + x].dose_rate_rhr;
            if (dose < fallout_threshold) continue;
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
                json_kv_str(w, "kind", "fallout");
                json_kv_num(w, "dose_rate_rhr", dose);
            json_obj_end(w);
            json_key(w, "geometry"); json_obj_begin(w);
                json_kv_str(w, "type", "Polygon");
                json_key(w, "coordinates"); json_arr_begin(w);
                    json_arr_begin(w);
                    for (int k = 0; k < 4; k++) { json_arr_begin(w); json_num(w, lon[k]); json_num(w, lat[k]); json_arr_end(w); }
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

/* ---- Teletype -------------------------------------------------------- */
void dmk_write_report_teletype(const DmkModel *m, const DmkCasualtyOpts *opts,
                               const DmkCasualties *cas, void *fpv) {
    FILE *fp = (FILE *)fpv;
    const DmkScenario *sc = &m->scenario;
    real_t vis = default_visibility(opts);

    fprintf(fp, "\n========================================================\n");
    fprintf(fp, "        DAMASKINO EFFECTS REPORT\n");
    fprintf(fp, "========================================================\n");
    fprintf(fp, "Location : %s (%.4f, %.4f)\n", sc->gz.name,
            (double)sc->gz.lat, (double)sc->gz.lon);
    fprintf(fp, "Weapon   : %.1f KT %s burst, fission %.0f%%\n",
            (double)sc->weapon.yield_kt, sc->weapon.is_surface_burst ? "SURFACE":"AIR",
            (double)sc->weapon.fission_fraction * 100.0);

    DmkEffectRing br[8], tr[8], rr[8];
    int nb = dmk_blast_rings(sc, br, 8);
    int nt = dmk_thermal_rings(sc, vis, tr, 8);
    int nr = dmk_radiation_rings(sc, rr, 8);

    fprintf(fp, "\n*** AIR BLAST (overpressure) ***\n");
    for (int i = 0; i < nb; i++)
        fprintf(fp, "  %5.0f psi : %6.2f km  | %s\n", (double)br[i].value, (double)br[i].range_km, br[i].label);
    fprintf(fp, "\n*** THERMAL RADIATION (visibility %.0f km) ***\n", (double)vis);
    for (int i = 0; i < nt; i++)
        fprintf(fp, "  %5.1f cal/cm2 : %6.2f km  | %s\n", (double)tr[i].value, (double)tr[i].range_km, tr[i].label);
    fprintf(fp, "\n*** INITIAL NUCLEAR RADIATION ***\n");
    for (int i = 0; i < nr; i++)
        fprintf(fp, "  %5.0f rem : %6.2f km  | %s\n", (double)rr[i].value, (double)rr[i].range_km, rr[i].label);

    DmkCrater cr = dmk_crater(sc->weapon.yield_kt, sc->weapon.is_surface_burst);
    if (cr.radius_m > 0.0)
        fprintf(fp, "\n*** CRATER ***\n  radius %.0f m, depth %.0f m (dry soil)\n",
                (double)cr.radius_m, (double)cr.depth_m);
    real_t emp_fp = dmk_hemp_footprint_km(sc->weapon.hob_m);
    if (emp_fp > 0.0)
        fprintf(fp, "\n*** HIGH-ALTITUDE EMP ***\n  footprint radius %.0f km, peak E1 ~%.0f kV/m\n",
                (double)emp_fp, (double)dmk_hemp_peak_field_kvm());

    if (cas) {
        fprintf(fp, "\n*** CASUALTY ESTIMATE ***\n");
        if (opts)
            fprintf(fp, "  (PF fallout=%.0f, PF prompt=%.0f, thermal-exposed=%.0f%%, %.0f h)\n",
                    (double)opts->pf_fallout, (double)opts->pf_prompt,
                    (double)opts->thermal_exposed_frac*100.0,
                    (double)opts->exposure_hours);
        fprintf(fp, "  Population in domain : %.0f\n", cas->population_in_domain);
        fprintf(fp, "  Estimated fatalities : %.0f\n", cas->fatalities);
        fprintf(fp, "  Estimated injuries   : %.0f\n", cas->injuries);
        fprintf(fp, "  By cause (non-excl.) : blast %.0f  thermal %.0f  prompt %.0f  fallout %.0f\n",
                cas->fatal_blast, cas->fatal_thermal, cas->fatal_prompt, cas->fatal_fallout);
    }
    fflush(fp);
}
