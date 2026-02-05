/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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
#include "neighbours5.h"

vj_effect *neighbours5_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = 32;	/* line size */
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 255;     /* smoothness */
    ve->limits[0][2] = 0; 	/* luma only / include chroma */
    ve->limits[1][2] = 1;
    ve->defaults[0] = 4;
    ve->defaults[1] = 5;
    ve->defaults[2] = 1;
    ve->description = "ZArtistic Filter (Vertical strokes)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Stroke size", "Smoothness", "Mode" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][2], 2, "Luma Only", "Luma and Chroma" );
    
	return ve;
}

typedef struct {
    int *row_hist[256];
    int *row_y_map[256];
    int *row_cb_map[256];
    int *row_cr_map[256];
    uint8_t *tmp_buf[2];
    uint8_t *chromacity[2];
    int width,height;
} nb_t;

typedef struct
{
	uint8_t y;
	uint8_t u;
	uint8_t v;
} pixel_t;


void *neighbours5_malloc(int w,int h)
{
    nb_t *n=(nb_t*)vj_calloc(sizeof(nb_t));
    if(!n) return NULL;

    n->width=w; n->height=h;

    n->tmp_buf[0]=(uint8_t*)vj_malloc(sizeof(uint8_t)*w*h*2);
    if(!n->tmp_buf[0]) { free(n); return NULL; }
    n->tmp_buf[1]=n->tmp_buf[0]+w*h;

    n->chromacity[0]=(uint8_t*)vj_malloc(sizeof(uint8_t)*w*h*2);
    if(!n->chromacity[0]) { free(n->tmp_buf[0]); free(n); return NULL; }
    n->chromacity[1]=n->chromacity[0]+w*h;

    for(int i=0;i<256;i++){
        n->row_hist[i]=(int*)vj_calloc(sizeof(int)*w);
        n->row_y_map[i]=(int*)vj_calloc(sizeof(int)*w);
        n->row_cb_map[i]=(int*)vj_calloc(sizeof(int)*w);
        n->row_cr_map[i]=(int*)vj_calloc(sizeof(int)*w);
    }

    return n;
}

void neighbours5_free(void *ptr)
{
    nb_t *n=(nb_t*)ptr;
    if(!n) return;

    for(int i=0;i<256;i++){
        free(n->row_hist[i]);
        free(n->row_y_map[i]);
        free(n->row_cb_map[i]);
        free(n->row_cr_map[i]);
    }

    free(n->tmp_buf[0]);
    free(n->chromacity[0]);
    free(n);
}

static inline int clamp_int(int val,int min,int max){ return val<min?min:(val>max?max:val); }

static inline uint8_t evaluate_pixel_row(
    int x,int y,int brush_size,int max_intensity,
    uint8_t *premul,uint8_t *Y,
    int *row_hist,int *row_y_map,int width,int height
){
    int peak_val=0,peak_idx=0;

    veejay_memset(row_hist, 0, max_intensity * sizeof(int));
    veejay_memset(row_y_map, 0, max_intensity * sizeof(int));;

    int y0=clamp_int(y-brush_size,0,height-1);
    int y1=clamp_int(y+brush_size,0,height-1);

    for(int j=y0;j<y1;j++){
        int idx=j*width+x;
        int bright=premul[idx];
        row_hist[bright]++;
        row_y_map[bright]+=Y[idx];
    }

    for(int i=0;i<max_intensity;i++)
        if(row_hist[i]>peak_val){ peak_val=row_hist[i]; peak_idx=i; }

    return peak_val>15 ? (uint8_t)(row_y_map[peak_idx]/peak_val) : Y[y*width+x];
}

static inline pixel_t evaluate_pixel_row_c(
    int x,int y,int brush_size,int max_intensity,
    uint8_t *premul,uint8_t *Y,uint8_t *Cb,uint8_t *Cr,
    int *row_hist,int *row_y_map,int *row_cb_map,int *row_cr_map,int width,int height
){
    pixel_t val;
    int peak_val=0,peak_idx=0;

    veejay_memset(row_hist, 0, max_intensity * sizeof(int));
    veejay_memset(row_y_map, 0, max_intensity * sizeof(int));
    veejay_memset(row_cb_map, 0, max_intensity * sizeof(int));
    veejay_memset(row_cr_map, 0, max_intensity * sizeof(int));

    int y0=clamp_int(y-brush_size,0,height-1);
    int y1=clamp_int(y+brush_size,0,height-1);

    for(int j=y0;j<y1;j++){
        int idx=j*width+x;
        int bright=premul[idx];
        row_hist[bright]++;
        row_y_map[bright]+=Y[idx];
        row_cb_map[bright]+=Cb[idx];
        row_cr_map[bright]+=Cr[idx];
    }

    for(int i=0;i<max_intensity;i++)
        if(row_hist[i]>peak_val){ peak_val=row_hist[i]; peak_idx=i; }

    if(peak_val>0){
        val.y=row_y_map[peak_idx]/peak_val;
        val.u=row_cb_map[peak_idx]/peak_val;
        val.v=row_cr_map[peak_idx]/peak_val;
    } else {
        int idx=y*width+x;
        val.y=Y[idx]; val.u=Cb[idx]; val.v=Cr[idx];
    }

    return val;
}

void neighbours5_apply(void *ptr,VJFrame *frame,int *args)
{
    int brush_size=args[0],intensity_level=args[1],mode=args[2];
    nb_t *n=(nb_t*)ptr;

    int width=frame->width,height=frame->height,len=frame->len;

    double intensity=intensity_level/255.0;
    int max_intensity=(int)(0xff*intensity);

    uint8_t *Y=n->tmp_buf[0],*Y2=n->tmp_buf[1];
    uint8_t *dstY=frame->data[0],*dstCb=frame->data[1],*dstCr=frame->data[2];

    vj_frame_copy1(frame->data[0],Y2,len);

    if(mode){
        int strides[3]={0,len,len};
        uint8_t *dest[3]={NULL,n->chromacity[0],n->chromacity[1]};
        vj_frame_copy(frame->data,dest,strides);
    }

#pragma omp simd
    for(int i=0;i<len;i++) Y[i]=(uint8_t)(Y2[i]*intensity);

    if (!mode) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                dstY[y * width + x] = evaluate_pixel_row(
                    x, y,                    // coordinates
                    brush_size,               // brush size
                    max_intensity,            // max intensity for histogram
                    Y,                        // premultiplied map
                    Y2,                       // original luma
                    n->row_hist[0],           // histogram buffer
                    n->row_y_map[0],          // y_map buffer
                    width, height             // frame dimensions
                );
            }
        }
        veejay_memset(frame->data[1], 128, len);
        veejay_memset(frame->data[2], 128, len);
    } 
    else {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                pixel_t tmp = evaluate_pixel_row_c(
                    x, y,
                    brush_size,
                    max_intensity,
                    Y, Y2,
                    n->chromacity[0], n->chromacity[1],
                    n->row_hist[0], n->row_y_map[0],
                    n->row_cb_map[0], n->row_cr_map[0],
                    width, height
                );
                dstY[y * width + x] = tmp.y;
                dstCb[y * width + x] = tmp.u;
                dstCr[y * width + x] = tmp.v;
            }
        }
    }
}

