/*
 * output_teletype.c - Human-readable 72-column teletype report.
 * Dependency-free (no JSON) so it links into the UNIVAC-lite profile.
 */
#include "damaskino.h"
#include "output.h"
#include <math.h>
#include <stdio.h>

static real_t td_dist_km(const DmkModel *m, int x, int y) {
    real_t dx = (x - m->gz_x) * m->grid.cell_km;
    real_t dy = (y - m->gz_y) * m->grid.cell_km;
    return sqrt(dx * dx + dy * dy);
}

void dmk_write_teletype(const DmkModel *m, void *fpv) {
    FILE *fp = (FILE *)fpv;
    const DmkScenario *sc = &m->scenario;

    fprintf(fp, "\n========================================================\n");
    fprintf(fp, "        DAMASKINO FALLOUT PREDICTION RESULTS\n");
    fprintf(fp, "========================================================\n");
    fprintf(fp, "Location : %s (%.4f, %.4f)\n", sc->gz.name,
            (double)sc->gz.lat, (double)sc->gz.lon);
    fprintf(fp, "Weapon   : %.1f KT %s burst, fission %.0f%%\n",
            (double)sc->weapon.yield_kt,
            sc->weapon.is_surface_burst ? "SURFACE" : "AIR",
            (double)sc->weapon.fission_fraction * 100.0);
    fprintf(fp, "Cloud    : top %.1f km, stem %.1f km, stabilize %.1f min\n",
            (double)m->cloud.cloud_top_km, (double)m->cloud.stem_diameter_km,
            (double)m->cloud.stabilization_min);

    fprintf(fp, "\n*** DOSE RATE MAP (R/HR AT H+%.0f) ***\n",
            (double)sc->cfg.ref_time_hr);
    fprintf(fp, "  . <1   : 1-10   + 10-50   * 50-100   "
                "# 100-500   @ 500-1000   X >1000\n\n");
    int step = m->grid.n / 64; if (step < 1) step = 1;
    for (int y = 0; y < m->grid.n; y += step) {
        for (int x = 0; x < m->grid.n; x += step) {
            char s;
            if (x == m->gz_x && y == m->gz_y) { fputc('G', fp); continue; }
            real_t d = m->grid.cell[(size_t)y*m->grid.n + x].dose_rate_rhr;
            if (d < 1) s = '.'; else if (d < 10) s = ':'; else if (d < 50) s = '+';
            else if (d < 100) s = '*'; else if (d < 500) s = '#';
            else if (d < 1000) s = '@'; else s = 'X';
            fputc(s, fp);
        }
        fputc('\n', fp);
    }

    fprintf(fp, "\n*** DOSE RATE CONTOURS ***\n");
    fprintf(fp, "Level (R/hr) | Max extent (km) | Area (km^2)\n");
    static const real_t levels[] = {10, 50, 100, 500, 1000, 5000};
    for (unsigned li = 0; li < sizeof(levels)/sizeof(levels[0]); li++) {
        real_t level = levels[li], max_ext = 0.0; long count = 0;
        for (int y = 0; y < m->grid.n; y++)
            for (int x = 0; x < m->grid.n; x++)
                if (m->grid.cell[(size_t)y*m->grid.n + x].dose_rate_rhr >= level) {
                    count++;
                    real_t dd = td_dist_km(m, x, y);
                    if (dd > max_ext) max_ext = dd;
                }
        fprintf(fp, "%11.0f  | %13.1f  | %10.1f\n",
                (double)level, (double)max_ext,
                (double)(count * m->grid.cell_km * m->grid.cell_km));
    }
    fflush(fp);
}
