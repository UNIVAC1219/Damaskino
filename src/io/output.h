#ifndef DMK_OUTPUT_H
#define DMK_OUTPUT_H
#include "damaskino.h"
#include "json.h"

/* Structured machine-readable result (summary, contours, sectors, timeline,
 * provenance). */
void dmk_write_result_json(const DmkModel *model, JsonWriter *w);

/* GeoJSON FeatureCollection of dose-rate cells above `threshold_rhr`. */
void dmk_write_geojson(const DmkModel *model, JsonWriter *w, real_t threshold_rhr);

/* Human-readable teletype-style report to a FILE (stdout). */
void dmk_write_teletype(const DmkModel *model, void *fp);

#endif /* DMK_OUTPUT_H */
