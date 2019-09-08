/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
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

/**
 *
 * Based on the original implementation of Kim Asendorf
 *
 * https://github.com/kimasendorf/ASDFPixelSort/blob/master/ASDFPixelSort.pde
 *
 * ASDFPixelSort
 * Processing script to sort portions of pixels in an image.
 * DEMO: http://kimasendorf.com/mountain-tour/ http://kimasendorf.com/sorted-aerial/
 * Kim Asendorf 2010 http://kimasendorf.com
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "pixelsort.h"

vj_effect *pixelsort_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 3;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 0xff;
    ve->defaults[0] = 0;
    ve->defaults[1] = 3;
    ve->defaults[2] = 40;
    ve->description = "Asendorf Glitch";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Columns first, or Rows first, Columns Only, Rows Only" , "Threshold" );
    return ve;
}

typedef struct {
    uint8_t *mask;
} pixelsort_t;

void *pixelsort_malloc(int w, int h){
    pixelsort_t *p = (pixelsort_t*) vj_calloc(sizeof(pixelsort_t));
    if(!p) {
        return NULL;
    }
    p->mask = (uint8_t*) vj_calloc(sizeof(uint8_t) * RUP8(w + w * h) );
    
    if(!p->mask) {
        free(p);
        return NULL;
    }

    return (void*) p;
}

void pixelsort_free(void *ptr) {
    pixelsort_t *p = (pixelsort_t*) ptr;
    free(p->mask);
    free(p);
}

static inline unsigned int firstNotBlackY(uint8_t *Y, unsigned int x, unsigned int y, unsigned int wid, unsigned int hei)
{
    while( Y[y * wid + x] <= pixel_Y_lo_ ) { /* skip black pixels in column */
        if( y >= hei )
            break;
        y ++;
    }

    return y;
}

static inline unsigned int nextBlackY(uint8_t *Y, unsigned int x, unsigned int y, unsigned int wid, unsigned int hei)
{
    while( Y[y * wid + x] > pixel_Y_lo_ ) { /* skip pixels until a black pixel is found */
        if( y >= hei )
            break;
        y ++;
    }

    return y;
}

static inline unsigned int firstNonWhiteY(uint8_t *Y, unsigned int x, unsigned int y, unsigned int wid, unsigned int hei)
{
    while( Y[y * wid + x] >= pixel_Y_hi_ ) { /* skip white pixels in column */
        if( y >= hei )
            break;
        y ++;
    }
    return y;
}

static inline unsigned int nextWhiteY(uint8_t * Y, unsigned int x, unsigned int y, unsigned int wid, unsigned int hei)
{
    while( Y[y * wid + x] < pixel_Y_hi_ ) { /* skip pixels until a white pixel is found */
        if( y >= hei )
            break;
        y ++;
    }
    return y;
}

static inline unsigned int firstNotBlackX(uint8_t *Y, unsigned int x, unsigned int y, unsigned int wid)
{
    while( Y[y * wid + x] <= pixel_Y_lo_ ) {
        if( x >= wid )
            break;
        x ++;
    }

    return x;
}

static inline unsigned int nextBlackX(uint8_t *Y, unsigned int x, unsigned int y, unsigned int wid)
{
    while( Y[y * wid + x] > pixel_Y_lo_ ) {
        if( x >= wid )
            break;
        x ++;
    }

    return x;
}

static inline unsigned int firstNonWhiteX(uint8_t *Y, unsigned int x, unsigned int y, unsigned int wid)
{
    while( Y[y * wid + x] >= pixel_Y_hi_ ) {
        if( x >= wid )
            break;
        x ++;
    }
    return x;
}

static inline unsigned int nextWhiteX(uint8_t * Y, unsigned int x, unsigned int y, unsigned int wid)
{
    while( Y[(y * wid + x)] < pixel_Y_hi_ ) {
        if( x >= wid )
            break;
        x ++;
    }
    return x;
}

/* counting sort
 *
 * this routine sorts only by the first byte in uint32_t (range is 0-255)
 *
 * this is to 'remember' what UVA values went with Y once it is done sorting
 *
 * byte 0-8: Y     <- sort by
 * byte 8-16: U
 * byte 16-24: V
 * byte 24-32: A
 * 
 */
static void csort32( uint32_t *input, size_t n, uint32_t *output ) 
{
    unsigned int i;
    unsigned int k = 256;
    uint16_t count[k];

    memset( count,0,sizeof(count));

    for( i = 0; i < n; i ++ )
        count[ (input[i] & 0xff) ] ++;

    for( i = 1; i < k; i ++ ) 
        count[i] += count[i-1];

    for( i = 0; i < n; i ++ ) {
        output[ count[ (input[i] & 0xff) ] - 1 ] = input[i];
        count[ (input[i] & 0xff) ] --;
    }
}
/* pack by column */
static void csort32_packY( uint8_t *p[4], uint32_t *dst, size_t n, unsigned int x, unsigned int y, unsigned int wid )
{
    unsigned int i,pos,dy=y;
    const uint8_t *Y = p[0];
    const uint8_t *U = p[1];
    const uint8_t *V = p[2];
    const uint8_t *A = p[3];
    for( i = 0; i < n; i ++, dy ++ ) {
        pos = (dy * wid) + x;
        dst[i] = (Y[pos] & 0xff) + ((U[pos] & 0xff) << 8) + ((V[pos] & 0xff) << 16) + ((A[pos] & 0xff) << 24);
    }
}

/* pack by column */
static void csort32_unpackY( uint8_t *p[4], uint32_t *src, size_t n, unsigned int x, unsigned int y, unsigned int wid)
{
    unsigned int i,pos,dy=y;
    uint8_t *Y = p[0];
    uint8_t *U = p[1];
    uint8_t *V = p[2];
    uint8_t *A = p[3];

    for( i = 0; i < n; i ++, dy ++ ) {
        pos = (dy * wid) + x;
        Y[pos] = (src[i] & 0xff);
        U[pos] = (src[i] >> 8) & 0xff;
        V[pos] = (src[i] >> 16) & 0xff;
        A[pos] = (src[i] >> 24) & 0xff;
    }
}
/* pack by row */
static void csort32_unpackX( uint8_t *p[4], uint32_t *src, size_t n, unsigned int x, unsigned int y, unsigned int wid)
{
    unsigned int i,pos,dx=x;
    uint8_t *Y = p[0];
    uint8_t *U = p[1];
    uint8_t *V = p[2];
    uint8_t *A = p[3];

    for( i = 0; i < n; i ++, dx ++ ) {
        pos = (y * wid) + dx;
        Y[pos] = (src[i] & 0xff);
        U[pos] = (src[i] >> 8) & 0xff;
        V[pos] = (src[i] >> 16) & 0xff;
        A[pos] = (src[i] >> 24) & 0xff;
    }
}
/* pack by row */
static void csort32_packX( uint8_t *p[4], uint32_t *dst, size_t n, unsigned int x, unsigned int y, unsigned int wid )
{
    unsigned int i,pos,dx=x;
    const uint8_t *Y = p[0];
    const uint8_t *U = p[1];
    const uint8_t *V = p[2];
    const uint8_t *A = p[3];
    for( i = 0; i < n; i ++,dx++ ) {
        pos = (y * wid) + dx;
        dst[i] = (Y[pos] & 0xff) + ((U[pos] & 0xff) << 8) + ((V[pos] & 0xff) << 16) + ((A[pos] & 0xff) << 24);
    }
}

static inline void sort_x(uint8_t *P[4], unsigned int width, unsigned int x, unsigned int y, unsigned int x_end)
{
    int sortlen = x_end - x;
    uint32_t sorted[sortlen];
    uint32_t unsorted[sortlen];

    csort32_packX( P, unsorted, sortlen, x, y, width );

    csort32( unsorted, sortlen, sorted );

    csort32_unpackX( P, sorted, sortlen, x, y, width );
}

static inline void sort_y(uint8_t *P[4], unsigned int width, unsigned int x, unsigned int y, unsigned int y_end) 
{
    int sortlen = (y_end - y);
    uint32_t sorted[sortlen];
    uint32_t unsorted[sortlen];

    csort32_packY( P, unsorted, sortlen, x, y, width );

    csort32( unsorted, sortlen, sorted );

    csort32_unpackY( P, sorted, sortlen, x, y, width );
}

static unsigned int pixelsort_column(uint8_t *mask, uint8_t *P[4], unsigned int width, unsigned int x1, unsigned int y1, unsigned int height, int mode)
{
    unsigned int x = x1;
    unsigned int y = y1;
    unsigned int y_end = y + 1;

    switch(mode) {
        case 0:
            y = firstNotBlackY(mask,x,y,width,height);
            y_end = nextBlackY(mask,x,y,width,height);
            break;
        case 1:
            y = nextWhiteY(mask,x,y,width,height);
            y_end = firstNonWhiteY(mask,x,y,width,height);
            break;
        case 2:
            y = firstNonWhiteY(mask,x,y,width,height);
            y_end = nextWhiteY(mask,x,y,width,height);
            break;

    }

    if(y_end < y) {
        veejay_msg(0, "Mode y_end %d before y %d",y_end,y );
        return y + 1;
    }
    
    sort_y(P,width,x,y,y_end);

    return y_end;
}

static unsigned int pixelsort_row(uint8_t *mask, uint8_t *P[4],unsigned int width, unsigned int x1, unsigned int y1, int mode)
{
    unsigned int x = x1;
    unsigned int y = y1;
    unsigned int x_end = x + 1;

    switch(mode) {
        case 0:
            x = firstNotBlackX(mask,x,y,width);
            x_end = nextBlackX(mask,x,y,width);
            break;
        case 1:
            x = nextWhiteX(mask,x,y,width);
            x_end = firstNonWhiteX(mask,x,y,width);
            break;
        case 2:
            x = firstNonWhiteX(mask,x,y,width);
            x_end = nextWhiteX(mask,x,y,width);
            break;
    }

    if(x_end < x) {
        veejay_msg(0, "Mode x_end %d before x %d",x_end,x);
        return x + 1;
    }
    
    sort_x(P,width,x,y,x_end);

    return x_end;
}


void pixelsort_apply( void *ptr, VJFrame *frame, int *args) {
    int mode = args[0];
    int rows1st = args[1];
    int threshold = args[2];

    pixelsort_t *p = (pixelsort_t*) ptr;

    uint8_t pixel_Y_hi_ = pixel_Y_hi_;
    uint8_t pixel_Y_lo_ = pixel_Y_lo_;
    unsigned int x=0,y=0;
    unsigned int wid = frame->width;
    unsigned int hei = frame->height;

    binarify_1src( p->mask, frame->data[0], threshold, 0, wid,hei );

    switch(rows1st) {
        case 0:
            for( y = 0; y < frame->height; y ++ ) {
                x = 0;
                while( x < wid ) {
                    x += pixelsort_row( p->mask, frame->data, frame->width, x,y, mode );
                }
            }
            for( x = 0; x < frame->width; x ++ ) {
                y = 0;
                while( y < hei ) {
                    y += pixelsort_column( p->mask, frame->data, frame->width, x, y, frame->height, mode );
                }
            }
        break;
        case 1:
            for( x = 0; x < frame->width; x ++ ) {
                y = 0;
                while( y < hei ) {
                    y += pixelsort_column( p->mask, frame->data, frame->width, x, y, frame->height, mode );
                }
            }
            for( y = 0; y < frame->height; y ++ ) {
                x = 0;
                while( x < wid ) {
                    x += pixelsort_row( p->mask, frame->data, frame->width, x,y, mode );
                }
            }
            break;
        case 2:
            for( x = 0; x < frame->width; x ++ ) {
                y = 0;
                while( y < hei ) {
                    y += pixelsort_column( p->mask, frame->data, frame->width, x, y, frame->height, mode );
                } 
            }
            break;
        case 3:
            for( y = 0; y < frame->height; y ++ ) {
                x = 0;
                while( x < wid ) {
                    x += pixelsort_row( p->mask, frame->data, frame->width, x,y, mode );
                }
            }
            break;
    }

}
