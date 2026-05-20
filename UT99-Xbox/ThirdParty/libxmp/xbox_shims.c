#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include "src/common.h"

#undef snprintf
#undef vsnprintf

int libxmp_get_filetype(const char *path)
{
    (void)path;
    return XMP_FILETYPE_NONE;
}

char *getenv(const char *name)
{
    (void)name;
    return NULL;
}

int libxmp_vsnprintf(char *str, size_t sz, const char *fmt, va_list ap)
{
    int rc = _vsnprintf(str, sz, fmt, ap);
    if (sz != 0) {
        if (rc < 0) rc = (int)sz;
        if ((size_t)rc >= sz) str[sz - 1] = '\0';
    }
    return rc;
}

int libxmp_snprintf(char *str, size_t sz, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = libxmp_vsnprintf(str, sz, fmt, ap);
    va_end(ap);

    return rc;
}
