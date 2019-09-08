/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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

/* distortion effects */
#include "common.h"
#include <veejaycore/vjmem.h>
#include "rotozoom.h"

typedef struct {
    int *test_roto[9];
    int *test_roto2[9];
    int new_zpath;
    int new_path;
    int roto_old_p;
    int roto_old_z;
    uint8_t *rotobuffer[4];
} rotozoom_t;

vj_effect *rotozoom_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;
    ve->defaults[2] = 1;
    ve->defaults[3] = 1;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 8;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->description = "Rotozoom";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Rotate", "Zoom" , "Automatic");
    ve->has_user = 0;

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0, "Normal", "Rotozoom II",
           "Rotozoom III", "Rotozoom IV", "Rotozoom V", "Rotozoom VI", "Rotozoom VII", "Rotozoom VIII", "Rotozoom IX");


    return ve;
}

void *rotozoom_malloc(int width, int height)
{
    int i;
    rotozoom_t *r = (rotozoom_t*) vj_calloc( sizeof(rotozoom_t) );
    if(!r) {
        return NULL;
    }

    r->rotobuffer[0] = (uint8_t *) vj_calloc(sizeof(uint8_t) * RUP8(width * height * 3));
    if(!r->rotobuffer[0]) {
        free(r);
        return NULL;
    }

    r->rotobuffer[1] = r->rotobuffer[0] + RUP8(width * height);
    r->rotobuffer[2] = r->rotobuffer[1] + RUP8(width * height);

    int j;
    for (j = 0; j < 9; j++) {
     r->test_roto[j] = (int *) vj_malloc(sizeof(int) * 256);
     r->test_roto2[j] = (int *) vj_malloc(sizeof(int) * 256);
     if(!r->test_roto[j] || r->test_roto2[j]) {
         rotozoom_free(r);
         return NULL;
     }
    }

    for (i = 0; i < 256; i++) {
    float rad = (float) i * 1.41176 * 0.0174532;
    float c = sin(rad);
    r->test_roto[0][i] = (c + 0.8) * 4096.0;
    r->test_roto2[0][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
    float rad = (float) i * 2.41176 * 0.0174532;
    float c = sin(rad);
    r->test_roto[1][i] = (c + 0.8) * 4096.0;
    r->test_roto2[1][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
    float rad = (float) i * 3.41576 * 0.0174532;
    float c = sin(rad);
    r->test_roto[2][i] = (c + 0.8) * 4096.0;
    r->test_roto2[2][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
    float rad = (float) i * 4.74176 * 0.0174532;
    float c = sin(rad);
    r->test_roto[3][i] = (c + 0.8) * 4096.0;
    r->test_roto2[3][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
    float rad = (float) i * 5.91176 * 0.0174532;
    float c = sin(rad);
    r->test_roto[4][i] = (c + 0.8) * 4096.0;
    r->test_roto2[4][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
    float rad = (float) i * 9.12345 * 0.0174532;
    float c = sin(rad);
    r->test_roto[5][i] = (c + 0.8) * 4096.0;
    r->test_roto2[5][i] = (2.0 * c) * 4096.0;
    }
    for (i = 0; i < 256; i++) {
    float rad = (float) i * 9.12345 * 0.0174532;
    float c = sin(rad);
    r->test_roto[6][i] = (c + 0.8) * 8096.0;
    r->test_roto2[6][i] = (2.0 * c) * 8096.0;
    }
    for (i = 0; i < 256; i++) {
    float rad = (float) i * 1.41176 * 0.0174532;
    float c = sin(rad);
    r->test_roto[7][i] = c * 4096.0;
    r->test_roto2[7][i] = c * 4096.0;
    }
    for (i = 0; i < 256; i++) {
    float rad = (float) i * 1.0 * 0.0174532;
    float c = sin(rad);
    r->test_roto[8][i] = c * 4096.0;
    r->test_roto2[8][i] = c * 4096.0;
    }



    return (void*) r;
}

void rotozoom_free(void *ptr) {

    rotozoom_t *r = (rotozoom_t*) ptr;

    if(r->rotobuffer[0])
        free(r->rotobuffer[0]);

    int j;
    for( j = 0; j < 9; j ++ ) {
        if( r->test_roto[j] )
            free(r->test_roto[j]);
        if( r->test_roto2[j] )
            free(r->test_roto2);
    }

    free(r);
}

/* rotozoomer, from the demo effects collection, works in supersampled YCbCr space.
   printf("Retro Rotozoom Effect - B. Ellacott, W.P. van Paassen - 2002\n");
 */
static void draw_tile(int stepx, int stepy, int zoom, int w, int h,
           uint8_t * src1[3], uint8_t * src2[3])
{

    int x, y, i, j, xd, yd, a, b, sx, sy;

    sx = sy = 0;
    xd = (stepx * zoom) >> 12;
    yd = (stepy * zoom) >> 12;

    for (j = 0; j < h; j++) {
    x = sx;
    y = sy;
    for (i = 0; i < w; i++) {
        a = (x >> 12) & 255;
        b = (y >> 12) & 255;
        src1[0][(j * w) + i] = src2[0][b * w + a];
        src1[1][(j * w) + i] = src2[1][b * w + a];
        src1[2][(j * w) + i] = src2[2][b * w + a];
        x += xd;
        y += yd;
    }
    sx -= yd;
    sy += xd;
    }
}

static void rotozoom2_apply(rotozoom_t *r, VJFrame *frame, uint8_t *data[3], int width,
             int height, int n, int p, int z)
{
    draw_tile(r->test_roto[n][p],
          r->test_roto[n][(p + 128) & 0xFF],
          r->test_roto2[n][z], width, height, frame->data, data);
}

static void rotozoom1_apply(rotozoom_t *r, VJFrame *frame, uint8_t *data[3], int w, int h,
             int n, int p, int z)
{

    if (r->roto_old_p != p) {
        r->roto_old_p = p;
        r->new_path = p & 255;
    }
    if (r->roto_old_z != z) {
        r->roto_old_z = z;
        r->new_zpath = z & 255;
    }

    draw_tile(
          r->test_roto[n][r->new_path],
          r->test_roto[n][(r->new_path + 128) & 0xff],
          r->test_roto2[n][r->new_zpath], w, h, frame->data, data);

    r->new_path = (r->new_path - 1) & 255;
    r->new_zpath = (r->new_zpath + 1) & 255;

}


void rotozoom_apply( void *ptr, VJFrame *frame, int *args )
{
    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;
    int strides[4] = {len ,len ,len ,0};

    int mode = args[0];
    int rotate = args[1];
    int zoom = args[2];
    int autom = args[3];

    rotozoom_t *r = (rotozoom_t*) ptr;

    switch (autom) {        
    case 0:
        vj_frame_copy( frame->data, r->rotobuffer, strides );
        rotozoom2_apply(r,frame, r->rotobuffer, width, height, mode, rotate, zoom);
        break;
    case 1:
        vj_frame_copy( frame->data,r->rotobuffer, strides );
        rotozoom1_apply(r,frame, r->rotobuffer, width, height, mode, rotate, zoom);
        break;
    }


}
