/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef VJ_MISC_H
#define VJ_MISC_H
#include <stdarg.h>
#include <stdint.h>
#include <veejay/defs.h>
#include <veejay/veejay.h>
#define VEEJAY_FILE_LIMIT (1048576 * 1900)

void	*vj_new_rtc_clock(void);

void	vj_rtc_wait(void *rclock, long time_stamp );

void	vj_rtc_close(void *rclock);

int vj_perform_screenshot2(void * info, uint8_t ** src);

unsigned long	vj_get_relative_time(void);

int	vj_perform_take_bg( void *info, uint8_t **src);

int	veejay_create_temp_file(const char *prefix, char *dst);

void	vj_get_yuv_template(VJFrame *src, int w, int h, int fmt);

void	vj_get_rgb_template(VJFrame *src, int w, int h );

void	vj_get_yuv444_template(VJFrame *src, int w, int h);
#endif
