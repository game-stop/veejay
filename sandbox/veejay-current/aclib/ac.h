/*
 * ac.h -- main aclib include
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef ACLIB_AC_H
#define ACLIB_AC_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/*************************************************************************/

/* CPU acceleration support flags, for use with ac_init(): */

#define AC_IA32ASM      0x0001  /* x86-32: standard assembly (no MMX) */
#define AC_AMD64ASM     0x0002  /* x86-64: standard assembly (no MMX) */
#define AC_CMOVE        0x0004  /* x86: CMOVcc instruction */
#define AC_MMX          0x0008  /* x86: MMX instructions */
#define AC_MMXEXT       0x0010  /* x86: MMX extended instructions (AMD) */
#define AC_3DNOW        0x0020  /* x86: 3DNow! instructions (AMD) */
#define AC_3DNOWEXT     0x0040  /* x86: 3DNow! instructions (AMD) */
#define AC_SSE          0x0080  /* x86: SSE instructions */
#define AC_SSE2         0x0100  /* x86: SSE2 instructions */
#define AC_SSE3         0x0200  /* x86: SSE3 instructions */

#define AC_NONE         0       /* No acceleration (vanilla C functions) */
#define AC_ALL          (~0)    /* All available acceleration */


/* Endianness flag: */
#define AC_LITTLE_ENDIAN        1
#define AC_BIG_ENDIAN           2

/*************************************************************************/

/* Library initialization function--MUST be called before any other aclib
 * functions are used!  `accel' selects the accelerations to enable:
 * AC_NONE, AC_ALL, or a combination of the other AC_* flags above.  The
 * value will always be masked to the acceleration options available on the
 * actual CPU, as returned by ac_cpuinfo().  Returns 1 on success, 0 on
 * failure.  This function can be called multiple times to change the set
 * of acceleration features to be used. */
extern int ac_init(int accel);

/* Returns the set of acceleration features supported by this CPU. */
extern int ac_cpuinfo(void);

/* Returns the endianness of this CPU (AC_BIG_ENDIAN or AC_LITTLE_ENDIAN). */
extern int ac_endian(void);

/* Utility routine to convert a set of flags to a descriptive string.  The
 * string is stored in a static buffer overwritten each call. */
extern const char *ac_flagstotext(int accel);

/*************************************************************************/

/* Acceleration-enabled functions: */

/* Optimized memcpy().  The copy direction is guaranteed to be ascending
 * (so ac_memcpy(ptr, ptr+1, size) will work). */
extern void *ac_memcpy(void *dest, const void *src, size_t size);

/* Average of two sets of data */
extern void ac_average(const uint8_t *src1, const uint8_t *src2,
                       uint8_t *dest, int bytes);

/* Weighted average of two sets of data (weight1+weight2 should be 65536) */
extern void ac_rescale(const uint8_t *src1, const uint8_t *src2,
                       uint8_t *dest, int bytes,
                       uint32_t weight1, uint32_t weight2);

/* Image format manipulation is available in aclib/imgconvert.h */

/*************************************************************************/

#endif  /* ACLIB_AC_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
