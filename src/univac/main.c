/*
 * univac/main.c - UNIVAC 1219B lite profile driver.
 *
 * Built with -DUNIVAC: float precision, small heap grid, teletype I/O, no
 * JSON / weather / DEM dependencies. Portable C89-ish so it can cross-compile
 * for vintage targets. Shares the engine core with the full profile.
 */
#include "damaskino.h"
#include "output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static char *trim_upper(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\n'||e[-1]=='\r')) *--e = '\0';
    for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
    return s;
}

static void banner(void) {
    printf("\n========================================================\n");
    printf("   DAMASKINO - WSEG-10 FALLOUT CALCULATOR (UNIVAC 1219B)\n");
    printf("========================================================\n");
    printf("Lite profile: float precision, %d-column teletype.\n", 72);
    printf("Enter QUIT at any prompt to exit.\n");
}

int main(void) {
    char line[80];
    banner();

    for (;;) {
        DmkScenario sc;
        dmk_scenario_defaults(&sc);
        sc.cfg.grid_n = 64;        /* fit vintage core */
        sc.cfg.cell_km = 2.0f;

        printf("\n-------------------- NEW SIMULATION --------------------\n");

        printf("WEAPON YIELD (KT): ");
        if (!fgets(line, sizeof line, stdin)) break;
        if (strcmp(trim_upper(line), "QUIT") == 0) break;
        float y = (float)atof(line);
        if (y <= 0.0f || y > 100000.0f) { printf("ERROR: 0 < yield <= 100000.\n"); continue; }
        sc.weapon.yield_kt = y;

        printf("BURST TYPE (SURFACE/AIR): ");
        if (!fgets(line, sizeof line, stdin)) break;
        char *b = trim_upper(line);
        if (strcmp(b, "QUIT") == 0) break;
        if (strstr(b, "AIR")) {
            sc.weapon.is_surface_burst = 0;
            sc.weapon.hob_m = 540.0f * (float)pow(sc.weapon.yield_kt/1000.0f, 0.4);
            sc.weapon.fission_fraction = 0.5f;
        } else {
            sc.weapon.is_surface_burst = 1;
            sc.weapon.hob_m = 0.0f;
            sc.weapon.fission_fraction = 1.0f;
        }

        printf("SURFACE WIND (SPEED@DIR, e.g. 15@270): ");
        if (!fgets(line, sizeof line, stdin)) break;
        if (strcmp(trim_upper(line), "QUIT") == 0) break;
        float spd = 0.0f, dir = 270.0f;
        char *at = strchr(line, '@');
        if (at) { *at = '\0'; spd = (float)atof(line); dir = (float)atof(at+1); }
        sc.atmosphere.layer[0].speed_kts = spd;
        sc.atmosphere.layer[0].direction_deg = dir;
        dmk_atmosphere_estimate_aloft(&sc.atmosphere);

        printf("\n*** COMPUTING ***\n");
        DmkModel model;
        if (dmk_run(&sc, &model) != 0) { printf("ERROR: engine failure.\n"); continue; }
        dmk_write_teletype(&model, stdout);
        dmk_model_free(&model);

        printf("\nRUN ANOTHER? (Y/N): ");
        if (!fgets(line, sizeof line, stdin) || toupper((unsigned char)line[0]) != 'Y') break;
    }

    printf("\n=============== SESSION TERMINATED ===============\n");
    return 0;
}
