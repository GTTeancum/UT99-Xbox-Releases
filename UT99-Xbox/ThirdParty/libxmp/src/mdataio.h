#ifndef LIBXMP_MDATAIO_H
#define LIBXMP_MDATAIO_H

#include <stddef.h>
#include "common.h"

static inline ptrdiff_t CAN_READ(MFILE *m)
{
	if (m->size >= 0)
		return m->pos >= 0 ? m->size - m->pos : 0;

	return INT_MAX;
}

static inline uint8 mread8(MFILE *m, int *err)
{
	uint8 x = 0xff;
	size_t r = mread(&x, 1, 1, m);
	if (err) {
	    *err = (r == 1) ? 0 : EOF;
	}
	return x;
}

static inline int8 mread8s(MFILE *m, int *err)
{
	int r = mgetc(m);
	if (err) {
	    *err = (r < 0) ? EOF : 0;
	}
	return (int8)r;
}

static inline uint16 mread16l(MFILE *m, int *err)
{
	ptrdiff_t can_read = CAN_READ(m);
	if (can_read >= 2) {
		uint8 buf[2];
		uint16 n;
		mread(buf, 1, 2, m);
		n = readmem16l(buf);
		if(err) *err = 0;
		return n;
	} else {
		m->pos += can_read;
		if(err) *err = EOF;
		return 0xffff;
	}
}

static inline uint16 mread16b(MFILE *m, int *err)
{
	ptrdiff_t can_read = CAN_READ(m);
	if (can_read >= 2) {
		uint8 buf[2];
		uint16 n;
		mread(buf, 1, 2, m);
		n = readmem16b(buf);
		if(err) *err = 0;
		return n;
	} else {
		m->pos += can_read;
		if(err) *err = EOF;
		return 0xffff;
	}
}

static inline uint32 mread24l(MFILE *m, int *err)
{
	ptrdiff_t can_read = CAN_READ(m);
	if (can_read >= 3) {
		uint8 buf[3];
		uint32 n;
		mread(buf, 1, 3, m);
		n = readmem24l(buf);
		if(err) *err = 0;
		return n;
	} else {
		m->pos += can_read;
		if(err) *err = EOF;
		return 0xffffffff;
	}
}

static inline uint32 mread24b(MFILE *m, int *err)
{
	ptrdiff_t can_read = CAN_READ(m);
	if (can_read >= 3) {
		uint8 buf[3];
		uint32 n;
		mread(buf, 1, 3, m);
		n = readmem24b(buf);
		if(err) *err = 0;
		return n;
	} else {
		m->pos += can_read;
		if(err) *err = EOF;
		return 0xffffffff;
	}
}

static inline uint32 mread32l(MFILE *m, int *err)
{
	ptrdiff_t can_read = CAN_READ(m);
	if (can_read >= 4) {
		uint8 buf[4];
		uint32 n;
		mread(buf, 1, 4, m);
		n = readmem32l(buf);
		if(err) *err = 0;
		return n;
	} else {
		m->pos += can_read;
		if(err) *err = EOF;
		return 0xffffffff;
	}
}

static inline uint32 mread32b(MFILE *m, int *err)
{
	ptrdiff_t can_read = CAN_READ(m);
	if (can_read >= 4) {
		uint8 buf[4];
		uint32 n;
		mread(buf, 1, 4, m);
		n = readmem32b(buf);
		if(err) *err = 0;
		return n;
	} else {
		m->pos += can_read;
		if(err) *err = EOF;
		return 0xffffffff;
	}
}

#endif
