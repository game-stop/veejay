/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2026 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#ifndef VJ_HIST_H
#define VJ_HIST_H

#include <stdint.h>
#include <math.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>

#ifdef __cplusplus
extern "C" {
#endif

void *veejay_histogram_new(void);

void veejay_histogram_del(void *his);

void veejay_histogram_analyze(void *his, VJFrame *f, int type);

void veejay_histogram_analyze_rgb(void *his, uint8_t *rgb, VJFrame *f);

void veejay_histogram_equalize(void *his, VJFrame *f, int intensity, int strength);

void veejay_histogram_equalize_rgb(void *his, VJFrame *f, uint8_t *rgb, int intensity, int strength, int mode);

void vje_histogram_auto_eq(VJFrame *frame);

void vje_histogram_auto_eq_serial(VJFrame *frame);

void veejay_histogram_draw(void *his, VJFrame *org, VJFrame *f, int intensity, int strength);

void veejay_histogram_draw_rgb(void *his, VJFrame *f, uint8_t *rgb, int in, int st, int mode);


#ifdef __cplusplus
}
#endif

#endif /* VJ_HIST_H */