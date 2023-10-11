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
#include "complexsaturate.h"

vj_effect *complexsaturation_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 4500; /* angle */
    ve->defaults[1] = 0;    /* r */
    ve->defaults[2] = 0;    /* g */
    ve->defaults[3] = 255;  /* b */
    ve->defaults[4] = 50;   /* v_adjust */
    ve->defaults[5] = 50;   /* degrees */
    ve->defaults[6] = 0; /* noise suppression */ 
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 9000;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 1;
    ve->limits[1][4] = 360;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 256;

    ve->limits[0][6] = 0;
    ve->limits[1][6] = 100;

    ve->has_user = 0;
    ve->description = "Complex Saturation (RGB)";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1; 
    ve->parallel = 1;
    ve->param_description = vje_build_param_list( ve->num_params, "Angle", "Red", "Green", "Blue", "Degrees", "Intensity", "Noise suppression" );
    return ve;
}

void complexsaturation_apply(void *ptr, VJFrame *frame,int *args ) {
    int i_angle = args[0];
    int r = args[1];
    int g = args[2];
    int b = args[3];
    int adjust_degrees = args[4];
    int adjust_v = args[5];
    int i_noise = args[6];

    uint8_t *Cb= frame->data[1];
    uint8_t *Cr= frame->data[2];
    
    const int len = frame->len;
    unsigned int pos;

    int iy = pixel_Y_lo_;
    int iu = 128;
    int iv = 128;

    float   hue = (adjust_degrees/180.0)*M_PI;
    float   sat = (adjust_v / 100.0f);

    _rgb2yuv( r,g,b, iy,iu,iv );
    
    int cb = (iu * 0xff) / 255;
    int cr = (iv * 0xff) / 255;
    int noiseThreshold = (i_noise * 255) / 100;
    noiseThreshold *= noiseThreshold;
    
    float angle = (float) (i_angle * 0.01) * (M_PI / 180.0f);
    int accept_angle_tg = (int)(15.0f * tanf(angle));

    const int s = (int) rint( a_sin(hue) * (1<<16) * sat );
    const int c = (int) rint( a_cos(hue) * (1<<16) * sat );
    
    for (pos = 0; pos < len; pos++)
    {
        int xx = (((Cb[pos]) * cb) + ((Cr[pos]) * cr)) >> 7;
        int yy = (((Cr[pos]) * cb) - ((Cb[pos]) * cr)) >> 7;

        /* accept angle should not be > 90 degrees 
           reasonable results between 10 and 80 degrees.
         */
        int distanceSquared = ( xx * xx ) + ( yy * yy );
        int val = (xx * accept_angle_tg) >> 4;

        if ((abs(yy) < val) && (distanceSquared >= noiseThreshold) ) { 
            /* pixel is within selected color range,  saturate */    
            int u = Cb[pos] - 128;
            int v = Cr[pos] - 128;
            
            int new_u = (c * u - s * v + (1<<15) + (128<<16)) >> 16;
            int new_v = (s * u + c * v + (1<<15) + (128<<16)) >> 16;
            if( new_u & 768 ) new_u = (-new_u) >> 31;
            if( new_v & 768 ) new_v = (-new_v) >> 31;
            Cb[pos] = new_u;
            Cr[pos] = new_v;
            
        }
    }
}
