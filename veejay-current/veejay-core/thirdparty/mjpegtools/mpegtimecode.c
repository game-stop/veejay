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

#include <config.h>
#include <stdlib.h>
#include "mpegtimecode.h"

/**************************************************************
 * // NTSC DROP FRAME TIMECODE / 29.97fps (SMTPE)
 * //    hh:mm:ss:ff
 * //       hh: 0..
 * //       mm: 0..59
 * //       ss: 0..59
 * //       ff: 0..29 # ss != 0 || mm % 10 == 0
 * //           2..29 # ss == 0 && mm % 10 != 0
 * //
 * // 00:00:00:00 00:00:00:01 00:00:00:02 ... 00:00:00:29
 * // 00:00:01:00 00:00:01:01 00:00:01:02 ... 00:00:01:29
 * //                        :
 * // 00:00:59:00 00:00:59:01 00:00:59:02 ... 00:00:59:29
 * //                         00:01:00:02 ... 00:01:00:29
 * // 00:01:01:00 00:01:01:01 00:01:01:02 ... 00:01:00:29
 * //                        :
 * // 00:01:59:00 00:01:59:01 00:01:59:02 ... 00:01:59:29
 * //                         00:02:00:02 ... 00:02:00:29
 * // 00:02:01:00 00:02:01:01 00:02:01:02 ... 00:02:00:29
 * //                        :
 * //                        :
 * // 00:09:59:00 00:09:59:01 00:09:59:02 ... 00:09:59:29
 * // 00:10:00:00 00:10:00:01 00:10:00:02 ... 00:10:00:29
 * // 00:10:01:00 00:10:01:01 00:10:01:02 ... 00:10:01:29
 * //                        :
 * // 00:10:59:00 00:10:59:01 00:10:59:02 ... 00:10:59:29
 * //                         00:11:00:02 ... 00:11:00:29
 * // 00:11:01:00 00:11:01:01 00:11:01:02 ... 00:11:00:29
 * //                        :
 * //                        :
 * // DROP FRAME / 59.94fps (no any standard)
 * // DROP FRAME / 23.976fps (no any standard)
 ***************************************************************/

int dropframetimecode = -1;

/* mpeg_timecode() return -tc->f on first frame in the minute, tc->f on other. */
int
mpeg_timecode(MPEG_timecode_t *tc, int f, int fpscode, double fps)
{
  static const int ifpss[] = { 0, 24, 24, 25, 30, 30, 50, 60, 60, };
  int h, m, s;

  int n_ifpss = (int)(sizeof ifpss / sizeof ifpss[0]);

  if (dropframetimecode < 0) {
    char *env = getenv("MJPEG_DROP_FRAME_TIME_CODE");
    dropframetimecode = (env && *env != '0' && *env != 'n' && *env != 'N');
  }

  int safe_fpscode = fpscode;
  if (safe_fpscode < 0 || safe_fpscode >= n_ifpss)
    safe_fpscode = 0;

  int ifps = ifpss[safe_fpscode];
  if (ifps <= 0)
    ifps = (int)(fps + 0.5);
  if (ifps <= 0)
    ifps = 25; /* final fallback to avoid FPE */

  if (dropframetimecode &&
      safe_fpscode + 1 < n_ifpss &&
      ifpss[safe_fpscode] == ifpss[safe_fpscode + 1] &&
      ifpss[safe_fpscode] > 0)
  {
    int topinmin = 0;

    int denom = ifpss[safe_fpscode];
    if (denom <= 0)
      denom = 30;

    int k = (30 * 4) / denom;
    if (k <= 0) k = 1; /* prevent division collapse */

    f *= k;

    h = (f / ((10*60*30-18)*4));
    f %= ((10*60*30-18)*4);
    f -= (2*4);

    m = (f / ((60*30-2)*4));
    topinmin = ((f - k) / ((60*30-2)*4) < m);

    m += (h % 6 * 10);
    h /= 6;

    f %= ((60*30-2)*4);
    f += (2*4);

    s = f / (30*4);
    f %= (30*4);
    f /= k;

    tc->f = f;
    if (topinmin)
      f = -f;
  }
  else
  {
    s = f / ifps;
    f %= ifps;

    m = s / 60;
    s %= 60;

    h = m / 60;
    m %= 60;

    tc->f = f;
  }

  tc->s = s;
  tc->m = m;
  tc->h = h;

  return f;
}
