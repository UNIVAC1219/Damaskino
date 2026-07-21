#ifndef DMK_OUTPUT_REPORT_H
#define DMK_OUTPUT_REPORT_H
#include "damaskino.h"
#include "casualties.h"
#include "json.h"

/* Comprehensive report: fallout + blast/thermal/prompt rings + casualties.
 * pop/opts/cas may be NULL to omit the casualty section. */
void dmk_write_report_json(const DmkModel *m, const DmkPopulation *pop,
                           const DmkCasualtyOpts *opts, const DmkCasualties *cas,
                           JsonWriter *w);

/* GeoJSON with fallout cells + effect-ring circles. */
void dmk_write_report_geojson(const DmkModel *m, const DmkCasualtyOpts *opts,
                              real_t fallout_threshold_rhr, JsonWriter *w);

/* Human report to a FILE* including rings and casualties. */
void dmk_write_report_teletype(const DmkModel *m, const DmkCasualtyOpts *opts,
                               const DmkCasualties *cas, void *fp);

#endif
