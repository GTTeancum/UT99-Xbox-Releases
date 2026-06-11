#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <xtl.h>
#include "src/common.h"

#undef snprintf
#undef vsnprintf
#undef malloc
#undef calloc
#undef realloc
#undef free

struct libxmp_xbox_alloc_header
{
    size_t size;
};

static unsigned long libxmp_xbox_live_bytes;
static unsigned long libxmp_xbox_peak_bytes;
static unsigned long libxmp_xbox_total_bytes;
static unsigned long libxmp_xbox_largest_bytes;
static unsigned long libxmp_xbox_fail_count;
static unsigned long libxmp_xbox_last_fail_bytes;
static unsigned long libxmp_xbox_last_fail_avail_kb;

static unsigned long libxmp_xbox_avail_kb(void)
{
    MEMORYSTATUS status;
    memset(&status, 0, sizeof(status));
    GlobalMemoryStatus(&status);
    return status.dwAvailPhys / 1024;
}

static void libxmp_xbox_note_alloc(size_t size)
{
    libxmp_xbox_live_bytes += (unsigned long)size;
    libxmp_xbox_total_bytes += (unsigned long)size;
    if (libxmp_xbox_live_bytes > libxmp_xbox_peak_bytes) {
        libxmp_xbox_peak_bytes = libxmp_xbox_live_bytes;
    }
    if ((unsigned long)size > libxmp_xbox_largest_bytes) {
        libxmp_xbox_largest_bytes = (unsigned long)size;
    }
}

static void libxmp_xbox_note_fail(size_t size)
{
    libxmp_xbox_fail_count++;
    libxmp_xbox_last_fail_bytes = (unsigned long)size;
    libxmp_xbox_last_fail_avail_kb = libxmp_xbox_avail_kb();
}

void *libxmp_xbox_malloc(size_t size)
{
    struct libxmp_xbox_alloc_header *header;
    size_t bytes = size + sizeof(*header);

    if (bytes < size) {
        libxmp_xbox_note_fail(size);
        return NULL;
    }

    header = (struct libxmp_xbox_alloc_header *)malloc(bytes);
    if (!header) {
        libxmp_xbox_note_fail(size);
        return NULL;
    }

    header->size = size;
    libxmp_xbox_note_alloc(size);
    return header + 1;
}

void *libxmp_xbox_calloc(size_t count, size_t size)
{
    void *ptr;
    size_t bytes = count * size;

    if (count != 0 && bytes / count != size) {
        libxmp_xbox_note_fail(bytes);
        return NULL;
    }

    ptr = libxmp_xbox_malloc(bytes);
    if (ptr) {
        memset(ptr, 0, bytes);
    }
    return ptr;
}

void *libxmp_xbox_realloc(void *ptr, size_t size)
{
    struct libxmp_xbox_alloc_header *old_header;
    struct libxmp_xbox_alloc_header *new_header;
    size_t old_size;
    size_t bytes;

    if (!ptr) {
        return libxmp_xbox_malloc(size);
    }
    if (size == 0) {
        libxmp_xbox_free(ptr);
        return NULL;
    }

    old_header = ((struct libxmp_xbox_alloc_header *)ptr) - 1;
    old_size = old_header->size;
    bytes = size + sizeof(*new_header);
    if (bytes < size) {
        libxmp_xbox_note_fail(size);
        return NULL;
    }

    new_header = (struct libxmp_xbox_alloc_header *)realloc(old_header, bytes);
    if (!new_header) {
        libxmp_xbox_note_fail(size);
        return NULL;
    }

    new_header->size = size;
    if (libxmp_xbox_live_bytes >= old_size) {
        libxmp_xbox_live_bytes -= (unsigned long)old_size;
    } else {
        libxmp_xbox_live_bytes = 0;
    }
    libxmp_xbox_note_alloc(size);
    return new_header + 1;
}

void libxmp_xbox_free(void *ptr)
{
    struct libxmp_xbox_alloc_header *header;

    if (!ptr) {
        return;
    }

    header = ((struct libxmp_xbox_alloc_header *)ptr) - 1;
    if (libxmp_xbox_live_bytes >= header->size) {
        libxmp_xbox_live_bytes -= (unsigned long)header->size;
    } else {
        libxmp_xbox_live_bytes = 0;
    }
    free(header);
}

void libxmp_xbox_reset_alloc_stats(void)
{
    libxmp_xbox_peak_bytes = libxmp_xbox_live_bytes;
    libxmp_xbox_total_bytes = 0;
    libxmp_xbox_largest_bytes = 0;
    libxmp_xbox_fail_count = 0;
    libxmp_xbox_last_fail_bytes = 0;
    libxmp_xbox_last_fail_avail_kb = 0;
}

void libxmp_xbox_get_alloc_stats(unsigned long *live, unsigned long *peak, unsigned long *total, unsigned long *largest, unsigned long *fails, unsigned long *last_fail, unsigned long *last_fail_avail_kb)
{
    if (live) *live = libxmp_xbox_live_bytes;
    if (peak) *peak = libxmp_xbox_peak_bytes;
    if (total) *total = libxmp_xbox_total_bytes;
    if (largest) *largest = libxmp_xbox_largest_bytes;
    if (fails) *fails = libxmp_xbox_fail_count;
    if (last_fail) *last_fail = libxmp_xbox_last_fail_bytes;
    if (last_fail_avail_kb) *last_fail_avail_kb = libxmp_xbox_last_fail_avail_kb;
}

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
