/*
 * test_lagrangian.c - Weather ingestion, wind interpolation, and the Lagrangian
 * fallout model (drift, wet vs dry deposition, dose integration).
 */
#include "damaskino.h"
#include "weather.h"
#include "casualties.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);failures++;} else printf("ok  : %s\n",msg);}while(0)

static void write_weather(const char *path, double precip) {
    FILE *f = fopen(path, "w");
    fprintf(f,
      "{ \"source\":\"synthetic\", \"lat\":38.9, \"lon\":-77.0, \"precip_mm_hr\":%.1f,"
      "  \"levels\":[ {\"altitude_m\":0,\"u_ms\":10,\"v_ms\":0},"
      "              {\"altitude_m\":5000,\"u_ms\":25,\"v_ms\":2},"
      "              {\"altitude_m\":12000,\"u_ms\":40,\"v_ms\":4} ] }", precip);
    fclose(f);
}

static double total_dose(const DmkModel *m) {
    double s = 0; for (int i=0;i<m->grid.n*m->grid.n;i++) s += m->grid.cell[i].dose_rate_rhr; return s;
}
static double east_minus_west(const DmkModel *m) {
    double e=0,wst=0;
    for (int y=0;y<m->grid.n;y++) for (int x=0;x<m->grid.n;x++){
        double d=m->grid.cell[(size_t)y*m->grid.n+x].dose_rate_rhr;
        if (x>m->gz_x) e+=d; else if (x<m->gz_x) wst+=d;
    }
    return e-wst;
}
static double contour_extent(const DmkModel *m, double level) {
    double mx=0; for (int y=0;y<m->grid.n;y++) for (int x=0;x<m->grid.n;x++)
        if (m->grid.cell[(size_t)y*m->grid.n+x].dose_rate_rhr>=level){
            double dx=(x-m->gz_x)*m->grid.cell_km, dy=(y-m->gz_y)*m->grid.cell_km;
            double d=sqrt(dx*dx+dy*dy); if(d>mx)mx=d;
        }
    return mx;
}

int main(void) {
    printf("=== Damaskino Lagrangian tests ===\n");

    /* Weather load + interpolation */
    const char *wp = "/tmp/wx_test.json";
    write_weather(wp, 0.0);
    DmkWindColumn w;
    CHECK(dmk_weather_load(wp, &w) == 0, "weather JSON loads");
    CHECK(w.nlev == 3, "3 levels loaded");
    CHECK(w.alt_m[0] < w.alt_m[1] && w.alt_m[1] < w.alt_m[2], "levels sorted ascending");
    real_t u, v;
    dmk_wind_at(&w, 2500.0, &u, &v);
    CHECK(fabs(u - 17.5) < 0.5, "wind interpolated at mid-level");
    dmk_wind_at(&w, -100.0, &u, &v);
    CHECK(fabs(u - 10.0) < 1e-6, "wind clamped below lowest level");
    dmk_wind_at(&w, 99999.0, &u, &v);
    CHECK(fabs(u - 40.0) < 1e-6, "wind clamped above highest level");
    dmk_weather_free(&w);

    /* Hand-entered atmosphere -> wind column conversion (from 270 -> eastward) */
    DmkScenario sc0; dmk_scenario_defaults(&sc0);
    sc0.atmosphere.layer[0].speed_kts = 20; sc0.atmosphere.layer[0].direction_deg = 270;
    dmk_atmosphere_estimate_aloft(&sc0.atmosphere);
    DmkWindColumn wc; dmk_weather_from_atmosphere(&sc0.atmosphere, &wc);
    dmk_wind_at(&wc, 0.0, &u, &v);
    CHECK(u > 0.0 && fabs(v) < fabs(u), "wind FROM 270 -> eastward u>0");
    dmk_weather_free(&wc);

    /* Lagrangian run: eastward wind -> plume east of GZ */
    DmkScenario sc; dmk_scenario_defaults(&sc);
    sc.gz.lat = 38.9; sc.gz.lon = -77.0;
    sc.weapon.yield_kt = 500; sc.weapon.is_surface_burst = 1; sc.weapon.fission_fraction = 1.0;
    sc.cfg.grid_n = 400; sc.cfg.cell_km = 2.0; sc.cfg.ref_time_hr = 1.0;  /* 800 km */

    DmkWindColumn dry; dmk_weather_load(wp, &dry);
    DmkModel md;
    CHECK(dmk_run_full(&sc, NULL, &dry, &md) == 0, "Lagrangian run succeeds");
    CHECK(total_dose(&md) > 0.0, "Lagrangian deposits dose");
    CHECK(east_minus_west(&md) > 0.0, "plume drifts downwind (east)");
    /* Fine particles form a worldwide-fallout tail; an 800 km grid holds the bulk. */
    CHECK(md.off_grid_fraction < 0.25, "bulk of activity retained on an 800 km grid");
    double dry_extent = contour_extent(&md, 10.0);
    dmk_weather_free(&dry);
    dmk_model_free(&md);

    /* Wet deposition: rainout concentrates the pattern (shorter far extent) */
    write_weather(wp, 10.0);
    DmkWindColumn wet; dmk_weather_load(wp, &wet);
    DmkModel mw; dmk_run_full(&sc, NULL, &wet, &mw);
    double wet_extent = contour_extent(&mw, 10.0);
    CHECK(wet_extent < dry_extent, "wet deposition shortens the 10 R/hr extent vs dry");
    printf("     (10 R/hr extent: dry %.1f km, wet %.1f km)\n", dry_extent, wet_extent);
    dmk_weather_free(&wet);
    dmk_model_free(&mw);

    /* Personal dose calculator (Way-Wigner integral) */
    double d48 = dmk_fallout_dose_rem(100.0, 1.0, 48.0, 1.0);   /* 100 R/hr H+1, unsheltered */
    double d48_pf10 = dmk_fallout_dose_rem(100.0, 1.0, 48.0, 10.0);
    CHECK(d48 > 0.0, "personal dose positive");
    CHECK(fabs(d48_pf10 - d48/10.0) < 1e-6, "protection factor divides dose");
    CHECK(dmk_fallout_dose_rem(100.0, 6.0, 48.0, 1.0) < d48, "later arrival -> less dose");

    printf("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED",
           failures, failures==1?"":"s");
    return failures ? 1 : 0;
}
