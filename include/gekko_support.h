#ifndef GEKKO_SUPPORT_H
#define GEKKO_SUPPORT_H

#ifdef GEKKO
#include <stdarg.h>
#include <stddef.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
char *
realpath(const char *path, char *resolved);
int
vsnprintf (char *s, size_t n, const char *format, va_list ap);
int
snprintf(char *str, size_t n, const char *fmt, ...);
long long
strtoq(const char *nptr, char **endptr, int base);
unsigned long long
strtouq(const char *nptr, char **endptr, int base);
float
strtof(const char *string, char **endPtr );
#ifdef __cplusplus
}
#endif
#endif

#endif // GEKKO_SUPPORT_H
