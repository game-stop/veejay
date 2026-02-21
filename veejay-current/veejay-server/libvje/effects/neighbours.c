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
#include "neighbours.h"

vj_effect *neighbours_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = 16;  /* brush size (shape is rectangle)*/
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 255;     /* smoothness */
    ve->limits[0][2] = 0;   /* luma only / include chroma */
    ve->limits[1][2] = 1;
    ve->defaults[0] = 4;
    ve->defaults[1] = 4;
    ve->defaults[2] = 0;
    ve->description = "ZArtistic Filter (Oilpainting, acc. add )";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Brush size", "Smoothness", "Mode" );

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][2], 2, "Luma Only", "Luma and Chroma" );

    return ve;
}

typedef struct
{
    uint8_t y;
    uint8_t u;
    uint8_t v;
} pixel_t;

typedef struct {
    int *row_hist[256];
    int *row_y_map[256];
    int *row_cb_map[256];
    int *row_cr_map[256];
    uint8_t *tmp_buf[2];
    uint8_t *chromacity[2];
    int width;
    int height;
    int brush_size_max;    
} nb_t;

void *neighbours_malloc(int w, int h) {
    nb_t *n = (nb_t*) vj_calloc(sizeof(nb_t));
    if(!n) return NULL;

    n->width = w;
    n->height = h;

    n->tmp_buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w*h*2));
    if(!n->tmp_buf[0]) { free(n); return NULL; }
    n->tmp_buf[1] = n->tmp_buf[0] + (w*h);

    n->chromacity[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w*h*2));
    if(!n->chromacity[0]) { free(n->tmp_buf[0]); free(n); return NULL; }
    n->chromacity[1] = n->chromacity[0] + (w*h);

    for(int i=0;i<256;i++){
        n->row_hist[i] = (int*) vj_calloc(sizeof(int) * w);
        n->row_y_map[i] = (int*) vj_calloc(sizeof(int) * w);
        n->row_cb_map[i] = (int*) vj_calloc(sizeof(int) * w);
        n->row_cr_map[i] = (int*) vj_calloc(sizeof(int) * w);
        if(!n->row_hist[i] || !n->row_y_map[i] || !n->row_cb_map[i] || !n->row_cr_map[i]){
            for(int j=0;j<=i;j++){
                if(n->row_hist[j]) free(n->row_hist[j]);
                if(n->row_y_map[j]) free(n->row_y_map[j]);
                if(n->row_cb_map[j]) free(n->row_cb_map[j]);
                if(n->row_cr_map[j]) free(n->row_cr_map[j]);
                    n->row_hist[i] = NULL;
                    n->row_y_map[i] = NULL;
                    n->row_cb_map[i] = NULL;
                    n->row_cr_map[i] = NULL;
            }
            free(n->chromacity[0]); free(n->tmp_buf[0]); free(n);
            return NULL;
        }
    }

    return (void*)n;
}

void neighbours_free(void *ptr){
    nb_t *n = (nb_t*) ptr;
    if(!n) return;

    for(int i=0;i<256;i++){
        if(n->row_hist[i]) { free(n->row_hist[i]); n->row_hist[i]=NULL; }
        if(n->row_y_map[i]) { free(n->row_y_map[i]); n->row_y_map[i]=NULL; }
        if(n->row_cb_map[i]) { free(n->row_cb_map[i]); n->row_cb_map[i]=NULL; }
        if(n->row_cr_map[i]) { free(n->row_cr_map[i]); n->row_cr_map[i]=NULL; }
    }

    if(n->tmp_buf[0]) { free(n->tmp_buf[0]); n->tmp_buf[0]=NULL; }
    if(n->chromacity[0]) { free(n->chromacity[0]); n->chromacity[0]=NULL; }
    free(n);
}

static inline int clamp(int val,int min,int max){return val<min?min:(val>max?max:val);}

static inline uint8_t evaluate_pixel_row(
    int x,int y,int brush_size,int max_intensity,
    uint8_t *premul,uint8_t *Y,
    int *row_hist,int *row_y_map,
    int width,int height
){
    int left = clamp(x - brush_size,0,width-1);
    int right = clamp(x + brush_size,0,width-1);
    int peak_value=0,peak_index=0;

    veejay_memset(row_hist, 0, max_intensity * sizeof(int));
    veejay_memset(row_y_map, 0, max_intensity * sizeof(int));;

    for(int j=left;j<=right;j++){
        int bright = premul[y*width + j];
        row_hist[bright]++;
        row_y_map[bright] += Y[y*width + j];
    }

    for(int i=0;i<max_intensity;i++){
        if(row_hist[i]>peak_value){ peak_value=row_hist[i]; peak_index=i; }
    }

    return peak_value ? (uint8_t)(row_y_map[peak_index]/peak_value) : Y[y*width + x];
}

static inline pixel_t evaluate_pixel_row_c(
    int x,int y,int brush_size,int max_intensity,
    uint8_t *premul,uint8_t *Y,uint8_t *Cb,uint8_t *Cr,
    int *row_hist,int *row_y_map,int *row_cb_map,int *row_cr_map,
    int width,int height
){
    pixel_t val;
    int left = clamp(x - brush_size,0,width-1);
    int right = clamp(x + brush_size,0,width-1);
    int peak_value=0,peak_index=0;

    veejay_memset(row_hist, 0, max_intensity * sizeof(int));
    veejay_memset(row_y_map, 0, max_intensity * sizeof(int));
    veejay_memset(row_cb_map, 0, max_intensity * sizeof(int));
    veejay_memset(row_cr_map, 0, max_intensity * sizeof(int));

    for(int j=left;j<=right;j++){
        int bright = premul[y*width + j];
        row_hist[bright]++;
        row_y_map[bright] += Y[y*width + j];
        row_cb_map[bright] += Cb[y*width + j];
        row_cr_map[bright] += Cr[y*width + j];
    }

    for(int i=0;i<max_intensity;i++){
        if(row_hist[i]>peak_value){ peak_value=row_hist[i]; peak_index=i; }
    }

    if(peak_value>0){
        val.y = row_y_map[peak_index]/peak_value;
        val.u = row_cb_map[peak_index]/peak_value;
        val.v = row_cr_map[peak_index]/peak_value;
    }else{
        val.y = Y[y*width + x];
        val.u = Cb[y*width + x];
        val.v = Cr[y*width + x];
    }

    return val;
}

void neighbours_apply(void *ptr,VJFrame *frame,int *args){
    int brush_size=args[0];
    int intensity_level=args[1];
    int mode=args[2];

    nb_t *n = (nb_t*) ptr;
    int width = frame->width;
    int height = frame->height;
    int len = frame->len;

    const double intensity = intensity_level/255.0;
    int max_intensity = (int)(0xff*intensity);

    uint8_t *Y = n->tmp_buf[0];
    uint8_t *Y2 = n->tmp_buf[1];
    uint8_t *dstY = frame->data[0];
    uint8_t *dstCb = frame->data[1];
    uint8_t *dstCr = frame->data[2];

    vj_frame_copy1(frame->data[0],Y2,len);
    if(mode){
        int strides[4]={0,len,len};
        uint8_t *dest[4]={NULL,n->chromacity[0],n->chromacity[1],NULL};
        vj_frame_copy(frame->data,dest,strides);
    }

#pragma omp simd
    for(int i=0;i<len;i++) Y[i]=(uint8_t)(Y2[i]*intensity);

    if(!mode){
        for(int y=0;y<height;y++){
            for(int x=0;x<width;x++){
                *(dstY++) = evaluate_pixel_row(
                    x,y,brush_size,max_intensity,Y,Y2,
                    n->row_hist[0],n->row_y_map[0],
                    width,height
                );
            }
        }
    }else{
        pixel_t tmp;
        for(int y=0;y<height;y++){
            for(int x=0;x<width;x++){
                tmp = evaluate_pixel_row_c(
                    x,y,brush_size,max_intensity,
                    Y,Y2,n->chromacity[0],n->chromacity[1],
                    n->row_hist[0],n->row_y_map[0],
                    n->row_cb_map[0],n->row_cr_map[0],
                    width,height
                );
                *(dstY++) = tmp.y;
                *(dstCb++) = tmp.u;
                *(dstCr++) = tmp.v;
            }
        }
    }
}