#ifndef DMK_VALIDATE_H
#define DMK_VALIDATE_H
/* Run validation-mode checks against a benchmark JSON file. Returns 0 if all
 * graded benchmarks pass, 1 if any fail, 2 on error. */
int dmk_run_validation(const char *path);
#endif
