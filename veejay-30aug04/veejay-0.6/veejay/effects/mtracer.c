/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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
#include <config.h>
#include "scratcher.h"
#include "common.h"
#include "magicoverlays.h"


extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);



uint8_t *mtrace_buffer[3];
static int mtrace_counter = 0;

vj_effect *mtracer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 30;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = MAX_SCRATCH_FRAMES;
    ve->defaults[0] = 150;
    ve->defaults[1] = 8;
    ve->description = "Magic Tracer";
    ve->sub_format = 0;
    ve->extra_frame = 1;
    ve->has_internal_data = 1;
    return ve;
}

void mtracer_free() {
  if(mtrace_buffer[0]) free(mtrace_buffer[0]);
  if(mtrace_buffer[1]) free(mtrace_buffer[1]);
  if(mtrace_buffer[2]) free(mtrace_buffer[2]);
}

int mtracer_malloc(int w, int h)
{
   mtrace_buffer[0] = (uint8_t *) vj_malloc(w * h * sizeof(uint8_t));
	if(!mtrace_buffer[0]) return 0;
    mtrace_buffer[1] = (uint8_t *) vj_malloc(w * h * sizeof(uint8_t));
	if(!mtrace_buffer[1]) return 0;
    mtrace_buffer[2] = (uint8_t *) vj_malloc(w * h * sizeof(uint8_t));
   	if(!mtrace_buffer[2]) return 0;
	return 1;
}

void mtracer_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
		   int width, int height, int mode, int n)
{

    unsigned int len = width * height;
    unsigned int uv_len = len >> 2;

    if (mtrace_counter == 0) {
	overlaymagic_apply(yuv1, yuv2, width, height, mode);
	veejay_memcpy(mtrace_buffer[0], yuv1[0], len);
	veejay_memcpy(mtrace_buffer[1], yuv1[1], uv_len);
	veejay_memcpy(mtrace_buffer[2], yuv1[2], uv_len);
    } else {
	overlaymagic_apply(yuv1, mtrace_buffer, width, height, mode);
	veejay_memcpy(mtrace_buffer[0], yuv1[0], len);
	veejay_memcpy(mtrace_buffer[1], yuv1[1], uv_len);
	veejay_memcpy(mtrace_buffer[2], yuv1[2], uv_len);

    }

    mtrace_counter++;
    if (mtrace_counter >= n)
	mtrace_counter = 0;


}
