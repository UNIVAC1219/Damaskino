/*
 * dmk_units.h - Physical constants and unit conversions for Damaskino.
 *
 * All internal computation is SI unless a struct field name says otherwise.
 * Values are sourced from Glasstone & Dolan, "The Effects of Nuclear Weapons"
 * (1977), NIST, and standard atmosphere references.
 */
#ifndef DMK_UNITS_H
#define DMK_UNITS_H

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Precision -------------------------------------------------------- */
/* UNIVAC-lite uses float to halve core footprint on the 1219B; the full
 * build uses double for numerical fidelity. real_t is used throughout the
 * engine so a single codebase serves both profiles. */
#ifdef UNIVAC
typedef float real_t;
#define RF "f"          /* printf length hint not needed for float promotion */
#else
typedef double real_t;
#define RF ""
#endif

/* ---- Length ----------------------------------------------------------- */
#define DMK_FT_TO_M        0.3048
#define DMK_M_TO_FT        3.280839895
#define DMK_KM_TO_M        1000.0
#define DMK_M_TO_KM        0.001
#define DMK_NM_TO_M        1852.0        /* nautical mile */
#define DMK_MI_TO_M        1609.344
#define DMK_MICRON_TO_M    1.0e-6

/* ---- Speed ------------------------------------------------------------ */
#define DMK_KNOTS_TO_MS    0.5144444444
#define DMK_MS_TO_KNOTS    1.9438444924

/* ---- Earth ------------------------------------------------------------ */
#define DMK_EARTH_RADIUS_M 6371000.0
#define DMK_DEG_TO_RAD     (M_PI / 180.0)
#define DMK_RAD_TO_DEG     (180.0 / M_PI)

/* ---- Atmosphere (US Standard / sea level, 15 C) ----------------------- */
#define DMK_GRAVITY_MS2    9.80665
#define DMK_AIR_RHO_SL     1.225        /* kg/m^3 */
#define DMK_AIR_MU         1.81e-5      /* Pa.s dynamic viscosity at 15 C */
#define DMK_SCALE_HEIGHT_M 8500.0       /* density e-folding height */

/* ---- Fallout particulate ---------------------------------------------- */
#define DMK_PARTICLE_RHO   2500.0       /* kg/m^3, silicate soil debris */

/* ---- Radiation (WSEG-10 normalization) -------------------------------- */
/* 1 MT fission at H+1 -> 1.6e6 R/hr at 1 nautical mile. */
#define DMK_REF_ACTIVITY_1MT_1HR  1.6e6
#define DMK_WAY_WIGNER_EXP        1.2   /* I(t) = I(1) * t^-1.2 */

/* ---- Energy partition (typical fission/thermonuclear) ----------------- */
/* Fractions of total yield. Glasstone & Dolan Table for air/surface bursts. */
#define DMK_FRAC_BLAST     0.50
#define DMK_FRAC_THERMAL   0.35
#define DMK_FRAC_PROMPT    0.05         /* initial nuclear radiation */
#define DMK_FRAC_RESIDUAL  0.10         /* fallout / delayed */

/* ---- Yield energy ------------------------------------------------------ */
#define DMK_KT_TO_JOULES   4.184e12     /* 1 kiloton TNT */

#endif /* DMK_UNITS_H */
