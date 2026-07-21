/* dmk_portable.h - tiny portability shim so the engine builds on MSVC (cl.exe)
 * as well as GCC/Clang.  MSVC lacks the POSIX <strings.h> case-insensitive
 * compares and does not define M_PI without _USE_MATH_DEFINES.
 *
 * Include this instead of <strings.h>. */
#ifndef DMK_PORTABLE_H
#define DMK_PORTABLE_H

#ifdef _MSC_VER
  #include <string.h>
  #define strcasecmp  _stricmp
  #define strncasecmp _strnicmp
#else
  #include <strings.h>   /* strcasecmp / strncasecmp on POSIX */
#endif

/* M_PI is not in ISO C; MSVC omits it unless _USE_MATH_DEFINES is set (the
 * build passes /D_USE_MATH_DEFINES, but define a fallback here too so any
 * translation unit that includes this header is safe regardless). */
#ifndef M_PI
  #define M_PI 3.14159265358979323846
#endif

#endif /* DMK_PORTABLE_H */
