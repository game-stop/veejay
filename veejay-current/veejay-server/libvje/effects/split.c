 /* Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "split.h"
static uint8_t *split_fixme[4] = { NULL,NULL,NULL, NULL };
vj_effect *split_init(int width,int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 8;
    ve->defaults[1] = 1;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 13;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->description = "Splitted Screens";
    ve->sub_format = 0;
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Switch");
    return ve;
}
int	split_malloc(int width, int height)
{
  split_fixme[0] = (uint8_t *) vj_calloc(sizeof(uint8_t) * width * height + 1);
  if(!split_fixme[0]) return 0;
    split_fixme[1] = (uint8_t *) vj_calloc(sizeof(uint8_t) * width * height );
  if(!split_fixme[1]) return 0; 
   split_fixme[2] = (uint8_t *) vj_calloc(sizeof(uint8_t) * width * height);
  if(!split_fixme[2]) return 0;
   return 1;

}

void split_free() {
  if(split_fixme[0]) free(split_fixme[0]);
  if(split_fixme[1]) free(split_fixme[1]);
  if(split_fixme[2]) free(split_fixme[2]);
}

void split_fib_downscale(VJFrame *frame, int width, int height)
{
    unsigned int i, len = frame->len/2;
    unsigned int f;
    unsigned int x, y;
    int uv_width;
    const int ilen = frame->len;
    const int uv_len = frame->uv_len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    i = 0;
    for (y = 0; y < len; y += width)
	{
		for (x = 0; x < width; x++)
		{
		    i++;
		    f = (i + 1) + (i - 1);
		    if( f >= ilen ) break;
		    Y[y + x] = Y[f];
		}
    }

    i = 0;
    uv_width = frame->uv_width;

    for (y = 0; y < uv_len; y += uv_width)
	{
		for (x = 0; x < uv_width; x++) {
		    i++;
		    f = (i + 1) + (i - 1);
	   		 if( f >= uv_len ) break;
	   		 Cb[y + x] = Cb[f];
	  		  Cr[y + x] = Cr[f];
		}
    }

}
void split_fib_downscaleb(VJFrame *frame, int width, int height)
{
    unsigned int len = frame->len / 2;
    unsigned int uv_len = frame->uv_len /2;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    split_fib_downscale(frame, width, height);

    int strides[4] = { len, uv_len, uv_len, 0 };
    uint8_t *output[4] = {
	Y + len,
	Cb + uv_len,
	Cr + uv_len,
   	NULL };

    vj_frame_copy( frame->data, output, strides );
}

void dosquarefib(VJFrame *frame, int width, int height)
{
    unsigned int i, len = frame->len / 2;
    unsigned int f;
    unsigned int x, y, y1, y2;
    unsigned int uv_len = frame->uv_len;
    unsigned int uv_width = frame->uv_width;
    const unsigned int uv_height = frame->uv_height;
    unsigned int w3 = width >> 2;
    const unsigned int u_w3 = w3 >> frame->shift_h;
    const unsigned int muv_len = frame->uv_len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

    i = 0;

    for (y = 0; y < len; y += width)
	{
		for (x = 0; x < width; x++) {
		    i++;
		    f = (i + 1) + (i - 1);
		    split_fixme[0][y + x] = Y[f];
		}
    }
    i = 0;
    for (y = 0; y < uv_len; y += uv_width) {
	for (x = 0; x < uv_width; x++) {
	    i++;
	    f = (i + 1) + (i - 1);
            if(f > muv_len) break;		 
	 //   split_fixme[0][y + x] = Y[f];
	    split_fixme[1][y + x] = Cb[f];
	    split_fixme[2][y + x] = Cr[f];
	}
    }

    len = len >> 1;
    width = width >> 1;
    for (y = 0; y < len; y += width) {
	for (x = 0; x < width; x++) {
	    i++;
	    f = (i + 1) + (i - 1);
	    split_fixme[0][y + x] = split_fixme[0][f];
	  //  split_fixme[0][y + x] = split_fixme[1][f];
	  //  split_fixme[0][y + x] = split_fixme[2][f];
	}
    }
    uv_len = uv_len >> 1;
    uv_width = uv_width >> 1;
     for (y = 0; y < uv_len; y += uv_width) {
	for (x = 0; x < uv_width; x++) {
	    i++;
	    f = (i + 1) + (i - 1);
	    if(f > muv_len) break;
	    split_fixme[1][y + x] = split_fixme[1][f];
	    split_fixme[2][y + x] = split_fixme[2][f];
	}
    }


    for (y = 0; y < height; y++) {
	y1 = (y * width) >> 1;
	y2 = y * width;
	for (i = 0; i < 4; i++)
	    for (x = 0; x < w3; x++) {
		Y[y2 + x + (i * w3)] = split_fixme[0][y1 + x];
	    }
    }
    for (y = 0; y < uv_height; y++) {
	y1 = (y * uv_width) >> 1;
	y2 = y * uv_width;
	for (i = 0; i < 4; i++)
	    for (x = 0; x < u_w3; x++) {
		Y[y2 + x + (i * u_w3)] = split_fixme[0][y1 + x];
	    }
    }

}

void split_push_downscale_uh(VJFrame *frame, int width, int height)
{
	unsigned int len = frame->len/2;
	int	strides[4] = { len,len,len ,0};
	vj_frame_copy( frame->data, split_fixme,strides );

}
void split_push_downscale_lh(VJFrame *frame, int width, int height)
{

    unsigned int x, y, y1, y2, j = 0;

    int hlen = height / 2;
    const int uv_width = frame->uv_width;
    const int uv_height = frame->uv_height;
    const int uv_hlen = frame->uv_len / 2;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

    for (y = 0; y < height; y+=2)
	{
		j++;
		y2 = j * width;
		y1 = y * width;
		for (x = 0; x < width; x++) {
		    split_fixme[0][y2 + x] = Y[y1 + x];
		}
    }

    j = 0;
    for (y = 0; y < uv_height; y+=2) {
	j++;
	y2 = j * uv_width;
	y1 = y * uv_width;
	for (x = 0; x < uv_width; x++) {
	    split_fixme[1][y2 + x] = Cb[y1 + x];
	    split_fixme[2][y2 + x] = Cr[y1 + x];
	}
    }

	int strides[4] = { hlen, uv_hlen, uv_hlen,0 };
	uint8_t *input[4] = { Y + hlen, Cb + uv_hlen, Cr + uv_hlen, NULL };
	vj_frame_copy( split_fixme, input, strides );
}

void split_push_vscale_left(VJFrame *frame, int width, int height)
{
    unsigned int x, y, y1;

    unsigned int wlen = width >> 1; //half
    const int uv_height = frame->uv_height;
    const int uv_width = frame->uv_width;
    const int uv_wlen = frame->uv_width >> frame->shift_h;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

    for (y = 0; y < height; y++)
	{
		y1 = y * width;
		for (x = 0; x < wlen; x++)
		{
	   	  split_fixme[0][y1 + x] = Y[y1 + (x << 1)];
		}
    }

    for (y = 0; y < uv_height; y++)
	{
		y1 = y * uv_width;
		for (x = 0; x < uv_wlen; x++)
		{
		    split_fixme[1][y1 + x] = Cb[y1 + (x << 1)];
		    split_fixme[2][y1 + x] = Cr[y1 + (x << 1)];
		}
    }


    for (y = 0; y < height; y++)
	{
		y1 = y * width;
		for (x = 0; x < wlen; x++)
		{
	   	 Y[y1 + x] = split_fixme[0][y1 + x];
		}
    }

    for (y = 0; y < uv_height; y++) {
	y1 = y * uv_width;
	for (x = 0; x < uv_wlen; x++) {
	    Cb[y1 + x] = split_fixme[1][y1 + x];
	    Cr[y1 + x] = split_fixme[2][y1 + x];
	}
    }


}
void split_push_vscale_right(VJFrame *frame, int width, int height)
{
    unsigned int x, y, y1;
    unsigned int wlen = width >> 1;
	const int uv_height = frame->uv_height;
    const int uv_width = frame->uv_width;
    const int uv_wlen = frame->uv_width / 2;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];


    for (y = 0; y < height; y++) {
	y1 = y * width;
	for (x = 0; x < wlen; x++) {
	    split_fixme[0][y1 + x] = Y[y1 + (x * 2)];
//	    split_fixme[1][y1 + x] = Cb[y1 + (x * 2)];
//	    split_fixme[2][y1 + x] = Cr[y1 + (x * 2)];
	}
    }
    for (y = 0; y < uv_height; y++) {
	y1 = y * uv_width;
	for (x = 0; x < uv_wlen; x++) {
//	    split_fixme[0][y1 + x] = Y[y1 + (x * 2)];
	    split_fixme[1][y1 + x] = Cb[y1 + (x * 2)];
	    split_fixme[2][y1 + x] = Cr[y1 + (x * 2)];
	}
    }

    for (y = 0; y < height; y++) {
	y1 = y * width;
	for (x = 0; x < wlen; x++) {
	    Y[y1 + x + wlen] = split_fixme[0][y1 + x];
//	    Cb[y1 + x + wlen] = split_fixme[1][y1 + x];
//	    Cr[y1 + x + wlen] = split_fixme[2][y1 + x];
	}
    }
    for (y = 0; y < uv_height; y++) {
	y1 = y * uv_width;
	for (x = 0; x < uv_wlen; x++) {
//	    Y[y1 + x + wlen] = split_fixme[0][y1 + x];
	    Cb[y1 + x + uv_wlen] = split_fixme[1][y1 + x];
	    Cr[y1 + x + uv_wlen] = split_fixme[2][y1 + x];
	}
    }

}



void split_corner_framedata_ul(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int w_len = width / 2;
    unsigned int h_len = height / 2;
    unsigned int x, y;
    unsigned int y1;
    const int uv_width = frame->uv_width;
    const int uv_wlen = frame->uv_width / 2;
    const int uv_hlen = frame->uv_height / 2;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (y = 0; y < h_len; y++) {
	y1 = width * y;
	for (x = 0; x < w_len; x++) {
	    Y[y1 + x] = Y2[y1 + x];
	}
    }
    for (y = 0; y < uv_hlen; y++) {
	y1 = uv_width * y;
	for (x = 0; x < uv_wlen; x++) {
	    Cb[y1 + x] = Cb2[y1 + x];
	    Cr[y1 + x] = Cr2[y1 + x];
	}
    }


}
void split_corner_framedata_ur(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int w_len = width / 2;
    unsigned int h_len = height / 2;
    unsigned int x, y;
    unsigned int y1;
    const int uv_width = frame->uv_width;
    const int uv_wlen = frame->uv_width / 2;
    const int uv_hlen = frame->uv_height / 2;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    for (y = 0; y < h_len; y++) {
	y1 = width * y;
	for (x = w_len; x < width; x++) {
	    Y[y1 + x] = Y2[y1 + x];
	 //   Cb[y1 + x] = Cb2[y1 + x];
	   // Cr[y1 + x] = Cr2[y1 + x];

	}
    }
    for (y = 0; y < uv_hlen; y++) {
	y1 = uv_width * y;
	for (x = uv_wlen; x < uv_width; x++) {
	  //  Y[y1 + x] = Y2[y1 + x];
	    Cb[y1 + x] = Cb2[y1 + x];
	    Cr[y1 + x] = Cr2[y1 + x];

	}
    }

}
void split_corner_framedata_dl(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int w_len = width / 2;
    unsigned int h_len = height / 2;
    unsigned int x, y;
    unsigned int y1;
    const int uv_height = frame->uv_height;
    const int uv_width = frame->uv_width;
    const int uv_wlen = frame->uv_width / 2;
    const int uv_hlen = frame->uv_height / 2;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];


    for (y = h_len; y < height; y++) {
	y1 = width * y;
	for (x = 0; x < w_len; x++) {
	    Y[y1 + x] = Y2[y1 + x];
	 //   Cb[y1 + x] = Cb2[y1 + x];
	 //   Cr[y1 + x] = Cr2[y1 + x];
	}
    }
    for (y = uv_hlen; y < uv_height; y++) {
	y1 = uv_width * y;
	for (x = 0; x < uv_wlen; x++) {
	    Cb[y1 + x] = Cb2[y1 + x];
	    Cr[y1 + x] = Cr2[y1 + x];
	}
    }

} 


void split_corner_framedata_dr(VJFrame *frame, VJFrame *frame2,
			     int width, int height)
{
    unsigned int w_len = width / 2;
    unsigned int h_len = height / 2;
    unsigned int x, y;
    unsigned int y1;
    const int uv_height = frame->uv_height;
    const int uv_width = frame->uv_width;
    const int uv_wlen = frame->uv_width / 2;
    const int uv_hlen = frame->uv_height / 2;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];


    for (y = h_len; y < height; y++) {
	y1 = width * y;
	for (x = w_len; x < width; x++) {
	    Y[y1 + x] = Y2[y1 + x];
//	    Cb[y1 + x] = Cb2[y1 + x];
//	    Cr[y1 + x] = Cr2[y1 + x];
	}
    }
    for (y = uv_hlen; y < uv_height; y++) {
	y1 = uv_width * y;
	for (x = uv_wlen; x < uv_width; x++) {
	    Cb[y1 + x] = Cb2[y1 + x];
	    Cr[y1 + x] = Cr2[y1 + x];
	}
    }

}


void split_v_first_halfs(VJFrame *frame, VJFrame *frame2, int width,
			 int height)
{

    unsigned int r, c;
    const int uv_height = frame->uv_height;
    const int uv_width = frame->uv_width;
    const int uv_len = uv_height * uv_width;

	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];


    for (r = 0; r < (width * height); r += width) {
	for (c = width / 2; c < width; c++) {
	    Y[c + r] = Y2[(width - c) + r];
	 //   Cb[c + r] = Cb2[(width - c) + r];
	 //   Cr[c + r] = Cr2[(width - c) + r];
	}
    }
    for (r = 0; r < uv_len; r += uv_width) {
	for (c = uv_width/2; c < uv_width; c++) {
	    Cb[c + r] = Cb2[(uv_width - c) + r];
	    Cr[c + r] = Cr2[(uv_width - c) + r];
	}
    }

}
void split_v_second_half(VJFrame *frame, VJFrame *frame2, int width,
			 int height)
{
    unsigned int r, c;
    const int uv_height = frame->uv_height;
    const int uv_width = frame->uv_width;
    const int uv_len = uv_height * uv_width;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2 = frame2->data[1];
	uint8_t *Cr2 = frame2->data[2];
	

    for (r = 0; r < (width * height); r += width) {
	for (c = width / 2; c < width; c++) {
	    Y[c + r] = Y2[c + r];
//	    Cb[c + r] = Cb2[c + r];
//	    Cr[c + r] = Cr2[c + r];
	}
    }


    for (r = 0; r < uv_len; r += uv_width) {
	for (c = uv_width / 2; c < uv_width; c++) {
	   // Y[c + r] = Y2[c + r];
	    Cb[c + r] = Cb2[c + r];
	    Cr[c + r] = Cr2[c + r];
	}
    }
}

void split_v_first_half(VJFrame *frame, VJFrame *frame2, int width,
			int height)
{
    unsigned int r, c;

    const int uv_height = frame->uv_height;
    const int uv_width = frame->uv_width;
    const int uv_len = uv_height * uv_width;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];


    for (r = 0; r < (width * height); r += width) {
	for (c = 0; c < width / 2; c++) {
	    Y[c + r] = Y2[c + r];
	//    Cb[c + r] = Cb2[c + r];
	//    Cr[c + r] = Cr2[c + r];
	}
    }

    for (r = 0; r < uv_len; r += uv_width) {
	for (c = 0; c < uv_width / 2; c++) {
//	    Y[c + r] = Y2[c + r];
	    Cb[c + r] = Cb2[c + r];
	    Cr[c + r] = Cr2[c + r];
	}
    }

}

void split_v_second_halfs(VJFrame *frame, VJFrame *frame2, int width,
			  int height)
{
    unsigned int r, c;
    const int lw = width / 2;
    const int len = frame->len;
    const int uv_height = frame->uv_height;
    const int uv_width = frame->uv_width;
    const int uv_len = uv_height * uv_width;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];


    for (r = 0; r < len; r += width) {
	for (c = 0; c < lw; c++) {
	    Y[c + r] = Y2[(width - c) + r];
//	    Cb[c + r] = Cb2[(width - c) + r];
//	    Cr[c + r] = Cr2[(width - c) + r];
	}
    }

    for (r = 0; r < uv_len; r += uv_width) {
	for (c = 0; c < (uv_width/2); c++) {
//	    Y[c + r] = Y2[(width - c) + r];
	    Cb[c + r] = Cb2[(width - c) + r];
	    Cr[c + r] = Cr2[(width - c) + r];
	}
    }

}

void split_h_first_half(VJFrame *frame, VJFrame *frame2, int width,
			int height)
{
   	const int len = frame->len / 2;
   	const int uv_len = frame->uv_len / 2;
	int strides[4] = { len,uv_len,uv_len, 0 };

	vj_frame_copy( frame2->data, frame->data, strides );    
}
void split_h_second_half(VJFrame *frame, VJFrame *frame2, int width,
			 int height)
{
	const unsigned int len = frame->len / 2;
	const unsigned int uv_len = frame->uv_len / 2;
	int strides[4] = { len, uv_len, uv_len, 0 };
	vj_frame_copy( frame2->data,frame->data, strides );
}
void split_h_first_halfs(VJFrame *frame, VJFrame *frame2, int width,
			 int height)
{
	const unsigned int len = frame->len / 2;
	const unsigned int uv_len = frame->uv_len / 2;
	int strides[4] = { len,uv_len,uv_len, 0 };
	vj_frame_copy( frame2->data, frame->data, strides );
}

void split_h_second_halfs(VJFrame *frame, VJFrame *frame2, int width,
			  int height)
{
	const unsigned int len = frame->len / 2;
	const unsigned int uv_len = frame->uv_len / 2;
	int strides[4] = { len, uv_len, uv_len, 0 };
	vj_frame_copy( frame2->data, frame->data, strides );
}

void split_apply(VJFrame *frame, VJFrame *frame2, int width,
		 int height, int n, int swap)
{
    switch (n) {
    case 0:
	if (swap)
	    split_push_downscale_uh(frame2, width, height);
	split_h_first_half(frame, frame2, width, height);
	break;
    case 1:
	//if (swap)
	  //  split_push_downscale_lh(frame2, width, height);
	split_h_second_half(frame, frame2, width, height);
	break;
    case 2:
	//if (swap)
	  //  split_push_downscale_lh(frame2, width, height);
	 /**/ split_h_first_halfs(frame, frame2, width, height);
	break;
    case 3:
	if (swap)
	    split_push_downscale_uh(frame2, width, height);
	 /**/ split_h_second_halfs(frame, frame2, width, height);
	break;
    case 4:
	if (swap)
	    split_push_vscale_left(frame2, width, height);
	 /**/ split_v_first_half(frame, frame2, width, height);
	break;
    case 5:
	if (swap)
	    split_push_vscale_right(frame2, width, height);
	 /**/ split_v_second_half(frame, frame2, width, height);
	break;
    case 6:
	if (swap)
	    split_push_vscale_left(frame2, width, height);
	 /**/ split_v_first_halfs(frame, frame2, width, height);
	break;

    case 7:
	//if (swap)
	    split_push_vscale_right(frame2, width, height);
	 // split_v_second_halfs(frame, frame2, width, height);
	break;
    case 8:
	if (swap)
	    split_fib_downscale(frame2, width, height);
	split_corner_framedata_ul(frame, frame2, width, height);
	break;
    case 9:
	if (swap)
	    split_fib_downscale(frame2, width, height);
	split_corner_framedata_ur(frame, frame2, width, height);
	break;
    case 10:
	if (swap)
	    split_fib_downscaleb(frame2, width, height);
	split_corner_framedata_dr(frame, frame2, width, height);
	break;
    case 11:
	if (swap)
	    split_fib_downscaleb(frame2, width, height);
	 /**/ split_corner_framedata_dl(frame, frame2, width, height);
	break;
    case 12:
	split_push_vscale_left(frame2, width, height);
	 /**/ split_push_vscale_right(frame, width, height);
	split_v_first_half(frame, frame2, width, height);
	break;
    case 13:
	split_push_downscale_uh(frame2, width, height);
	 // split_push_downscale_lh(frame, width, height);
	split_h_first_half(frame, frame2, width, height);
	break;
    }

}
