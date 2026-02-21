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
#include "neighbours4.h"

vj_effect *neighbours4_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = 32;	/* radius */
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 200;     /* distance from center */
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 255;	/* smoothness */
    ve->limits[0][3] = 0; 	/* luma only / include chroma */
    ve->limits[1][3] = 1;
    ve->defaults[0] = 4;
    ve->defaults[1] = 4;
    ve->defaults[2] = 8;
    ve->defaults[3] = 1;
    ve->description = "ZArtistic Filter (Round Brush)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Distance from center","Smoothness", "Mode" );
	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][3], 3, "Luma Only", "Luma and Chroma" );


    return ve;
}

typedef struct
{
	double x;
	double y;
} relpoint_t;

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
    relpoint_t points[2048];
    int width;
    int height;
} nb_t;


void *neighbours4_malloc(int w, int h)
{
    nb_t *n = (nb_t*) vj_calloc(sizeof(nb_t));
    if(!n) return NULL;

    n->width = w; n->height = h;

    n->tmp_buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t)*w*h*2);
    if(!n->tmp_buf[0]) { free(n); return NULL; }
    n->tmp_buf[1] = n->tmp_buf[0] + w*h;

    n->chromacity[0] = (uint8_t*) vj_malloc(sizeof(uint8_t)*w*h*2);
    if(!n->chromacity[0]) { free(n->tmp_buf[0]); free(n); return NULL; }
    n->chromacity[1] = n->chromacity[0] + w*h;

    for(int i=0;i<256;i++){
        n->row_hist[i] = (int*) vj_calloc(sizeof(int)*w);
        n->row_y_map[i] = (int*) vj_calloc(sizeof(int)*w);
        n->row_cb_map[i] = (int*) vj_calloc(sizeof(int)*w);
        n->row_cr_map[i] = (int*) vj_calloc(sizeof(int)*w);
        if(!n->row_hist[i]||!n->row_y_map[i]||!n->row_cb_map[i]||!n->row_cr_map[i]){
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

    return n;
}

void neighbours4_free(void *ptr)
{
    nb_t *n=(nb_t*)ptr;
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

static void create_circle(double radius,int depth,relpoint_t *points){
    for(int i=0;i<depth;i++){
        double angle=2.0*M_PI*i/depth;
        points[i].x=a_cos(angle)*radius;
        points[i].y=a_sin(angle)*radius;
    }
}

static inline int clamp_int(int val,int min,int max){return val<min?min:(val>max?max:val);}

static inline uint8_t evaluate_pixel_row(
    int x,int y,int brush_size,int max_intensity,
    uint8_t *premul,uint8_t *Y,const relpoint_t *pts,
    int *row_hist,int *row_y_map,int width,int height
){
    int peak_val=0,peak_idx=0;
    veejay_memset(row_hist, 0, max_intensity * sizeof(int));
    veejay_memset(row_y_map, 0, max_intensity * sizeof(int));

    for(int i=0;i<brush_size;i++){
        int dx=clamp_int((int)(pts[i].x+x),0,width-1);
        int dy=clamp_int((int)(pts[i].y+y),0,height-1);
        int idx=dy*width+dx;
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
    const relpoint_t *pts,int *row_hist,int *row_y_map,int *row_cb_map,int *row_cr_map,
    int width,int height
){
    pixel_t val; int peak_val=0,peak_idx=0;
    veejay_memset(row_hist, 0, max_intensity * sizeof(int));
    veejay_memset(row_y_map, 0, max_intensity * sizeof(int));
    veejay_memset(row_cb_map, 0, max_intensity * sizeof(int));
    veejay_memset(row_cr_map, 0, max_intensity * sizeof(int));

    for(int i=0;i<brush_size;i++){
        int dx=clamp_int((int)(pts[i].x+x),0,width-1);
        int dy=clamp_int((int)(pts[i].y+y),0,height-1);
        int idx=dy*width+dx;
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
    }else{
        int idx=y*width+x;
        val.y=Y[idx]; val.u=Cb[idx]; val.v=Cr[idx];
    }
    return val;
}

void neighbours4_apply(void *ptr,VJFrame *frame,int *args){
    int radius=args[0],brush_size=args[1],intensity_level=args[2],mode=args[3];
    nb_t *n=(nb_t*)ptr;
    int width=frame->width,height=frame->height,len=frame->len;

    double intensity=intensity_level/255.0;
    int max_intensity=(int)(0xff*intensity);

    uint8_t *Y=n->tmp_buf[0],*Y2=n->tmp_buf[1];
    uint8_t *dstY=frame->data[0],*dstCb=frame->data[1],*dstCr=frame->data[2];

    vj_frame_copy1(frame->data[0],Y2,len);
    create_circle((double)radius,brush_size,n->points);

    const relpoint_t *pts=n->points;

    if(mode){
        int strides[3]={0,len,len};
        uint8_t *dest[3]={NULL,n->chromacity[0],n->chromacity[1]};
        vj_frame_copy(frame->data,dest,strides);
    }

#pragma omp simd
    for(int i=0;i<len;i++) Y[i]=(uint8_t)(Y2[i]*intensity);

    if(!mode){
        for(int y=0;y<height;y++)
            for(int x=0;x<width;x++)
                dstY[y*width+x]=evaluate_pixel_row(x,y,brush_size,max_intensity,Y,Y2,pts,n->row_hist[0],n->row_y_map[0],width,height);
        veejay_memset(frame->data[1],128,len);
        veejay_memset(frame->data[2],128,len);
    }else{
        for(int y=0;y<height;y++)
            for(int x=0;x<width;x++){
                pixel_t tmp=evaluate_pixel_row_c(
                    x,y,brush_size,max_intensity,Y,Y2,n->chromacity[0],n->chromacity[1],
                    pts,n->row_hist[0],n->row_y_map[0],n->row_cb_map[0],n->row_cr_map[0],
                    width,height
                );
                dstY[y*width+x]=tmp.y;
                dstCb[y*width+x]=tmp.u;
                dstCr[y*width+x]=tmp.v;
            }
    }
}

