/* -*- mode:C -*- */
/*
 *  Copyright (C) 2001 Kawamata/Hitoshi <hitoshi.kawamata@nifty.ne.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __MPEGTIMECODE_H__
#define __MPEGTIMECODE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char h, m, s, f;
} MPEG_timecode_t;

extern int dropframetimecode;
extern int mpeg_timecode(MPEG_timecode_t *tc, int f, int fpscode, double fps);
/* mpeg_timecode() return -tc->f on first frame in the minute, tc->f on other. */

#ifdef __cplusplus
}
#endif

#endif
