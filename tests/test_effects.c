/*
 * test_effects.c - Benchmark the instant-effects models against canonical
 * Glasstone & Dolan values.
 */
#include "damaskino.h"
#include "effects.h"
#include <stdio.h>
#include <math.h>

static int failures = 0;
static void check_near(const char *name, double got, double want, double tol_frac) {
    double lo = want * (1.0 - tol_frac), hi = want * (1.0 + tol_frac);
    int ok = (got >= lo && got <= hi);
    printf("%s: %-46s got %8.3f  want %7.3f +/-%.0f%%\n",
           ok ? "ok  " : "FAIL", name, got, want, tol_frac * 100);
    if (!ok) failures++;
}
static void check_true(const char *name, int cond) {
    printf("%s: %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

int main(void) {
    printf("=== Damaskino effects benchmarks ===\n");

    /* Blast: 1 Mt optimum air burst, canonical Glasstone ground ranges. */
    check_near("1 Mt air, 5 psi",  dmk_blast_range_km(1000, 0, 5.0),  6.9,  0.10);
    check_near("1 Mt air, 20 psi", dmk_blast_range_km(1000, 0, 20.0), 3.1,  0.10);
    check_near("1 Mt air, 1 psi",  dmk_blast_range_km(1000, 0, 1.0),  17.5, 0.10);

    /* Cube-root scaling: Hiroshima ~15 kt air burst, 5 psi. */
    check_near("15 kt air, 5 psi", dmk_blast_range_km(15, 0, 5.0),
               6.9 * cbrt(15.0/1000.0), 0.05);

    /* Overpressure inverse is consistent with the forward map. */
    {
        double r = dmk_blast_range_km(1000, 0, 5.0);
        double p = dmk_blast_overpressure_psi(1000, 0, r);
        check_near("blast inverse round-trip (5 psi)", p, 5.0, 0.05);
    }

    /* Peak wind behind a 5 psi shock ~ 160 mph (~72 m/s). */
    check_near("blast wind at 5 psi", dmk_blast_wind_ms(5.0), 72.0, 0.10);

    /* Thermal: 1 Mt air burst, 20 km visibility, 3rd-degree (8 cal/cm^2)
     * lands in the ~7-10 km range. */
    {
        double r = dmk_thermal_range_km(1000, 0, 8.0, 20.0);
        check_true("1 Mt air 3rd-degree burns in 6-12 km", r > 6.0 && r < 12.0);
        printf("     (3rd-degree burn radius = %.2f km)\n", r);
        /* fluence falls off with range */
        check_true("thermal fluence decreases with range",
                   dmk_thermal_fluence(1000,0,5,20) > dmk_thermal_fluence(1000,0,10,20));
        /* worse visibility -> shorter range */
        check_true("lower visibility shortens thermal range",
                   dmk_thermal_range_km(1000,0,8,10) < dmk_thermal_range_km(1000,0,8,40));
    }

    /* Initial radiation: ~15 kt pure fission, 500 rem near ~1.1 km. */
    {
        double r = dmk_prompt_range_km(15, 1.0, 500.0);
        check_true("15 kt fission 500 rem in 0.8-1.4 km", r > 0.8 && r < 1.4);
        printf("     (500 rem radius = %.2f km)\n", r);
        /* dose decreases with range and increases with fission fraction */
        check_true("prompt dose decreases with range",
                   dmk_prompt_dose_rem(15,1.0,0.5) > dmk_prompt_dose_rem(15,1.0,1.5));
        check_true("prompt dose scales with fission fraction",
                   dmk_prompt_dose_rem(100,1.0,1.0) > dmk_prompt_dose_rem(100,0.5,1.0));
    }

    /* Surface vs air burst blast differ in the expected directions. */
    check_true("surface burst shorter 1 psi range than air",
               dmk_blast_range_km(1000,1,1.0) < dmk_blast_range_km(1000,0,1.0));

    printf("\n%s (%d failure%s)\n", failures ? "BENCHMARKS FAILED" : "ALL BENCHMARKS PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
