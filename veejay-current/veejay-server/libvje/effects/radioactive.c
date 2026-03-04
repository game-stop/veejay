/* 
 * EffecTV - Realtime Digital Video Effector
 * RadioacTV - motion-enlightment effect.
 * I referred to "DUNE!" by QuoVadis for this effect.
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * Veejay FX 'RadioActiveVJ'
 * (C) 2007 Niels Elburg
 *   This effect was ported from EffecTV.
 *   Differences:
 *    - difference frame over 2 frame interval intsead of bg substraction
 *    - several mask methods
 *    - more parameters
 *    - no palette (but mixing source)
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
//#include <libavutil/avutil.h>
#include <veejaycore/yuvconv.h>
#include "softblur.h"
#include "radioactive.h"

vj_effect *radioactivetv_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;  /* methods */
    ve->limits[1][0] = 6;
    ve->limits[0][1] = 50;// zoom ratio
    ve->limits[1][1] = 100;
    ve->limits[0][2] = 0; // strength 
    ve->limits[1][2] = 255; 
    ve->limits[0][3] = 0; //diff threhsold
    ve->limits[1][3] = 255;
    ve->defaults[0] = 0;
    ve->defaults[1] = 95;
    ve->defaults[2] = 200;
    ve->defaults[3] = 30;
    ve->description = "RadioActive EffecTV";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Zoom ratio", "Strength", "Difference Threshold" );
	
    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
			"Average", "Normal", "Strobe", "Spill (greyscale)", "Flood (greyscale)", "Frontal (greyscale)", "Low (greyscale)" ); 

    return ve;
}

//FIXME too slow

typedef struct {
    uint8_t *diffbuf;
	uint8_t *blurzoombuf;
    int *zoom_offset_x;
	int	*blurzoomx;
    int	*blurzoomy;
    int	buf_width_blocks;
	int	buf_width;
    int	buf_height;
    int	buf_area;
    int	buf_margin_left;
    int	buf_margin_right;
    int	first_frame;
    int	last_mode; // -1;
	float ratio_; // 0.95;
    int n_threads;
} radioactive_t;

static void setTable(radioactive_t *r)
{
    int x, y;
    float ratio_ = r->ratio_;
    int buf_width = r->buf_width;
    int buf_height = r->buf_height;
    
    const int HWIDTH = buf_width / 2;
    const int HHEIGHT = buf_height / 2;

    int tx = (int)(0.5 + ratio_ * (-HWIDTH) + HWIDTH);
    for(x = 0; x < buf_width; x++) {
        int ptr = (int)(0.5 + ratio_ * (x - HWIDTH) + HWIDTH);
        r->zoom_offset_x[x] = ptr - tx; 
    }

    int ty = (int)(0.5 + ratio_ * (-HHEIGHT) + HHEIGHT);
    int xx = (int)(0.5 + ratio_ * (buf_width - 1 - HWIDTH) + HWIDTH);
    
    r->blurzoomy[0] = ty * buf_width + tx; 
    int prev_row_end_ptr = ty * buf_width + xx; 
    
    for(y = 1; y < buf_height; y++){
        ty = (int)(0.5 + ratio_ * (y - HHEIGHT) + HHEIGHT);
        r->blurzoomy[y] = ty * buf_width + tx - prev_row_end_ptr;
        prev_row_end_ptr = ty * buf_width + xx;
    }
}
static void kentaro_blur(radioactive_t *r)
{
    const int width  = r->buf_width;
    const int height = r->buf_height;
    uint8_t *restrict src = r->blurzoombuf;
    uint8_t *restrict dst = r->blurzoombuf + r->buf_area;

    #pragma omp parallel for num_threads(r->n_threads)
    for (int y = 1; y < height - 1; y++) {
        const uint8_t *restrict top = src + (y - 1) * width;
        const uint8_t *restrict mid = src + y * width;
        const uint8_t *restrict bot = src + (y + 1) * width;
        uint8_t *restrict out = dst + y * width;

        #pragma omp simd
        for (int x = 1; x < width - 1; x++) {
            uint32_t v = (top[x] + mid[x - 1] + mid[x + 1] + bot[x]) >> 2;
            out[x] = (uint8_t)(v - !!v);
        }
    }
}

static void apply_blend(uint8_t *restrict dst, const uint8_t *restrict src, const uint8_t *restrict alpha_map, int len) {
    #pragma omp simd
    for (int i = 0; i < len; i++) {
        uint8_t a = alpha_map[i];
        uint8_t inv_a = 255 - a;
        dst[i] = (uint8_t)(dst[i] + ((a * (src[i] - dst[i])) >> 8));
    }
}

static void zoom(radioactive_t *r)
{
    const int height = r->buf_height;
    const int width = r->buf_width;
    uint8_t *base_in  = r->blurzoombuf + r->buf_area;
    uint8_t *base_out = r->blurzoombuf;

    int p_start_offsets[height];
    int current_p_offset = 0;
    
    int row_dx = r->zoom_offset_x[width - 1]; 

    for (int y = 0; y < height; y++) {
        current_p_offset += r->blurzoomy[y];
        p_start_offsets[y] = current_p_offset;
        current_p_offset += row_dx;
    }

    #pragma omp parallel for num_threads(r->n_threads)
    for (int y = 0; y < height; y++) {
        uint8_t *p_row = base_in + p_start_offsets[y];
        uint8_t *q_row = base_out + y * width;

        #pragma omp simd
        for (int x = 0; x < width; x++) {
            q_row[x] = p_row[ r->zoom_offset_x[x] ];
        }
    }
}

static void blurzoomcore(radioactive_t *r)
{
	kentaro_blur(r);
	zoom(r);
}

void *radioactivetv_malloc(int w, int h)
{
    if( (w/32) > 255 ) return NULL;

    radioactive_t *r = (radioactive_t*) vj_calloc(sizeof(radioactive_t));
    if(!r) return NULL;

    r->ratio_ = 0.95f;
    r->last_mode = -1;
    r->buf_width_blocks = (w / 32 );
    r->buf_width = r->buf_width_blocks * 32;
    r->buf_height = h;
    r->buf_area = r->buf_width * r->buf_height;
    r->buf_margin_left = (w - r->buf_width ) >> 1;
    r->buf_margin_right = (w - r->buf_width - r->buf_margin_left);
    
    r->blurzoombuf = (uint8_t*) vj_calloc( (r->buf_area * 2 ));
    r->blurzoomx = (int*) vj_calloc( r->buf_width_blocks * sizeof(int));
    r->blurzoomy = (int*) vj_calloc( r->buf_height * sizeof(int));

    r->zoom_offset_x = (int*)     vj_calloc(r->buf_width * sizeof(int));
    r->diffbuf      = (uint8_t*) vj_calloc(2 * w * h);

    r->n_threads = vje_advise_num_threads(w*h);

    if(!r->blurzoombuf || !r->zoom_offset_x || !r->blurzoomy || !r->diffbuf) {
        radioactivetv_free(r);
        return NULL;
    }

    setTable(r);
    return (void*) r;
}

void radioactivetv_free(void *ptr)
{
    radioactive_t *r = (radioactive_t*) ptr;
    if(r) {
        if(r->blurzoombuf)  free(r->blurzoombuf);
        if(r->zoom_offset_x) free(r->zoom_offset_x);
        if(r->blurzoomy)    free(r->blurzoomy);
        if(r->diffbuf)      free(r->diffbuf);
        free(r);
    }
}

static inline int radioactive_abs(int v) {
    int mask = v >> (sizeof(int) * 8 - 1);
    return (v ^ mask) - mask;
}

static inline void inject_motion_core(radioactive_t *r, uint8_t *lum, uint8_t *prev, 
                                      int width, int threshold, int snapInterval, int mode) 
{
    #pragma omp parallel for num_threads(r->n_threads)
    for (int y = 0; y < r->buf_height; y++) {
        int offset = y * width + r->buf_margin_left;
        uint8_t *restrict l_ptr = lum + offset;
        uint8_t *restrict p_ptr = prev + offset;
        uint8_t *restrict b_ptr = r->blurzoombuf + y * r->buf_width;

        #pragma omp simd
        for (int x = 0; x < r->buf_width; x++) {
            int d_val;
            
            // gcc inline magic optimizes the ifs away ...
            if (mode == 0 || mode == 3) {
                d_val = (p_ptr[x] + (l_ptr[x] * 3)) >> 2;
            } else if (mode == 1 || mode == 4) {
                int diff = l_ptr[x] - p_ptr[x];
                d_val = (diff < 0) ? 0 : (diff >> 1);
            } else if (mode == 6) {
                int delta = radioactive_abs(l_ptr[x] - p_ptr[x]);
                d_val = (delta > threshold) ? (l_ptr[x] >> 2) : 0;
            } else {
                d_val = radioactive_abs(l_ptr[x] - p_ptr[x]);
            }

            uint8_t motion = (uint8_t)(d_val * (d_val >= threshold));
            uint8_t existing = b_ptr[x] - !!b_ptr[x];
            b_ptr[x] = existing | ((motion * snapInterval) >> 7);
        }
    }
}

void radioactivetv_apply(void *ptr, VJFrame *frame, VJFrame *blue, int *args) {
    radioactive_t *r = (radioactive_t*) ptr;
    if (!r) return;

    int mode = args[0];
    float snapRatio = args[1] * 0.01f;
    int snapInterval = args[2];
    int threshold = args[3];

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *lum = frame->data[0];
    uint8_t *diff_base = r->diffbuf; 
    uint8_t *prev = diff_base + len;
    
    if (r->ratio_ != snapRatio) {
        r->ratio_ = snapRatio;
        setTable(r);
    }

    if (r->last_mode != mode || snapInterval == 0) {
        veejay_memset(r->blurzoombuf, 0, 2 * r->buf_area);
        r->last_mode = mode;
    }

    switch(mode) {
        case 0: case 3:
            inject_motion_core(r, lum, prev, width, threshold, snapInterval, 0);
            break;
        case 1: case 4:
            inject_motion_core(r, lum, prev, width, threshold, snapInterval, 1);
            break;
        case 6:
            inject_motion_core(r, lum, prev, width, threshold, snapInterval, 6);
            break;
        default: // Strobe (2, 5)
            inject_motion_core(r, lum, prev, width, threshold, snapInterval, 2);
            break;
    }
    // Update state for next frame
    veejay_memcpy(prev, lum, len); 

    // Execute the spatial effects
    kentaro_blur(r);
    zoom(r); 

    // Final Output Pass
    if (mode >= 3) {
        veejay_memset(frame->data[1], 128, len);
        veejay_memset(frame->data[2], 128, len);
        #pragma omp parallel for num_threads(r->n_threads)
        for (int y = 0; y < r->buf_height; y++) {
            veejay_memcpy(lum + (y * width) + r->buf_margin_left, 
                          r->blurzoombuf + (y * r->buf_width), r->buf_width);
        }
    } else { 
        #pragma omp parallel for num_threads(r->n_threads)
        for (int y = 0; y < r->buf_height; y++) {
            int frame_offset = (y * width) + r->buf_margin_left;
            uint8_t *restrict mask_row = r->blurzoombuf + (y * r->buf_width);
            uint8_t *restrict y_plane = frame->data[0] + frame_offset;
            uint8_t *restrict u_plane = frame->data[1] + frame_offset;
            uint8_t *restrict v_plane = frame->data[2] + frame_offset;
            uint8_t *restrict src_y = blue->data[0] + frame_offset;
            uint8_t *restrict src_u = blue->data[1] + frame_offset;
            uint8_t *restrict src_v = blue->data[2] + frame_offset;

            #pragma omp simd
            for (int x = 0; x < r->buf_width; x++) {
                uint8_t a = mask_row[x];
                if (a > 0) {
                    y_plane[x] += (a * (src_y[x] - y_plane[x])) >> 8;
                    u_plane[x] += (a * (src_u[x] - u_plane[x])) >> 8;
                    v_plane[x] += (a * (src_v[x] - v_plane[x])) >> 8;
                }
            }
        }
    }
}