#ifndef DMK_SCENARIO_IO_H
#define DMK_SCENARIO_IO_H
#include "damaskino.h"

/* Parse a scenario from a JSON document text. Returns 0 on success; on error
 * returns nonzero and (if errbuf) writes a message. Unspecified fields take
 * the defaults from dmk_scenario_defaults(). */
int dmk_scenario_parse(const char *json_text, DmkScenario *out,
                       char *errbuf, int errlen);

/* Read a whole file into a malloc'd NUL-terminated buffer (caller frees). */
char *dmk_read_file(const char *path);

#endif
