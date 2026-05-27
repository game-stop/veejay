/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "overclock.h"

vj_effect *overclock_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 2;
    ve->limits[1][0] = h / 8;
    ve->defaults[0] = 5;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 90;
    ve->defaults[1] = 2;

    ve->description = "Radial cubics";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Radius",
        "Value"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 2,  (h / 12 > 2 ? h / 12 : 2), 6, 22, 2200, 5200, 1800, 25, /* Radius */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 1,  32,                         6, 22, 1800, 4200, 900,  25  /* Value */
    );

    (void) w;

    return ve;
}

typedef struct {
    uint8_t *oc_buf;
} overclock_t;

void *overclock_malloc(int w, int h)
{
    overclock_t *o = (overclock_t*) vj_calloc(sizeof(overclock_t));
    if(!o)
        return NULL;

    o->oc_buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h);
    if(!o->oc_buf) {
        free(o);
        return NULL;
    }

    return (void*) o;
}

void overclock_free(void *ptr)
{
    overclock_t *o = (overclock_t*) ptr;
    if(!o)
        return;

    if(o->oc_buf)
        free(o->oc_buf);

    free(o);
}

static inline int overclock_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

void overclock_apply(void *ptr, VJFrame *frame, int *args)
{
    overclock_t *o = (overclock_t*) ptr;
    if(!o || !frame || !args)
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 1 || height <= 1 || len <= 0)
        return;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict B = o->oc_buf;

    int n = args[0];
    int radius = args[1];

    n = overclock_clampi(n, 1, height / 8 > 1 ? height / 8 : 1);
    radius = overclock_clampi(radius, 1, 90);

    int N = n << 1;
    if(N < 2)
        N = 2;

    const int max_block = width < height ? width : height;
    if(N > max_block)
        N = max_block;

    if(N < 1)
        return;

    for(int y = 0; y < height; y++) {
        veejay_blur2(
            B + (y * width),
            Y + (y * width),
            width,
            radius,
            1,
            1,
            1
        );
    }

    if(height <= (N << 1))
        return;

    for(int y = N; y < height - N; ) {
        const int r = 1 + (rand() % N);

        for(int x = 0; x < width; x += r) {
            const int bw = (x + N <= width) ? N : (width - x);
            const int bh = (y + N <= height) ? N : (height - y);

            if(bw <= 0 || bh <= 0)
                break;

            int sum = 0;
            const int area = bw * bh;

            for(int dy = 0; dy < bh; dy++) {
                const int row = (y + dy) * width + x;

                for(int dx = 0; dx < bw; dx++) {
                    sum += B[row + dx];
                }
            }

            const uint8_t t = (uint8_t)(sum / area);

            for(int dy = 0; dy < bh; dy++) {
                const int row = (y + dy) * width + x;

                for(int dx = 0; dx < bw; dx++) {
                    const int i = row + dx;
                    Y[i] = (B[i] > Y[i]) ? (uint8_t)((Y[i] + t) >> 1) : t;
                }
            }
        }

        y += 1 + (rand() % N);
    }
}