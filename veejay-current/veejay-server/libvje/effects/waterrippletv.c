/* EffecTV - Realtime Digital Video Effektor
 * Copyright (C) 2001-2003 FUKUCHI Kentaro
 *
 * RippleTV - Water ripple effect
 * Copyright (C) 2001 - 2002 FUKUCHI Kentaro
 * 
 * ported to Linux VeeJay by:
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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "waterrippletv.h"

typedef struct {
    uint8_t *ripple_data[3];
    int stat;
    signed char *vtable;
    int *map;
    int *map1;
    int *map2;
    int *map3;
    int map_h;
    int map_w;
    int sqrtable[256];
    int point;
    int impact;
    int tick;
    unsigned int wfastrand_val;
    int last_fresh_rate;
} ripple_tv;

/* from EffecTV:
 * fastrand - fast fake random number generator
 * Warning: The low-order bits of numbers generated by fastrand()
 *          are bad as random numbers. For example, fastrand()%4
 *          generates 1,2,3,0,1,2,3,0...
 *          You should use high-order bits.
 */

static unsigned int wfastrand(ripple_tv *r)
{
	return (r->wfastrand_val=r->wfastrand_val*1103515245+12345);
}

static void setTable(ripple_tv *r)
{
	int i;

	for(i=0; i<128; i++) {
		r->sqrtable[i] = i*i;
	}
	for(i=1; i<=128; i++) {
		r->sqrtable[256-i] = -i*i;
	}
}



vj_effect *waterrippletv_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 3600;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 16;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 32;
    ve->defaults[0] = 25*60;
    ve->defaults[1] = 1;
    ve->defaults[2] = 8;
    ve->description = "RippleTV  (EffectTV)";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Refresh Frequency", "Wavespeed", "Decay" );    
return ve;
}

void  *waterrippletv_malloc(int width, int height)
{
    ripple_tv *r = (ripple_tv*) vj_calloc(sizeof(ripple_tv));
    if(!r) {
        return NULL;
    }
	r->ripple_data[0] = (uint8_t*)vj_malloc(sizeof(uint8_t) * (width * height));
	if(!r->ripple_data[0]) {
        free(r);
        return NULL;
    }

	veejay_memset( r->ripple_data[0], pixel_Y_lo_, width*height);

	r->map_h = height / 2 + 1;
	r->map_w = width / 2 + 1;
	r->map = (int*) vj_calloc (sizeof(int) * (r->map_h * r->map_w * 3));
	if(!r->map) {
        free(r->ripple_data[0]);
        free(r);
        return NULL;
    }
	r->vtable = (signed char*) vj_calloc( sizeof(signed char) * (r->map_w * r->map_h * 2));
	if(!r->vtable) {
        free(r->ripple_data[0]);
        free(r->map);
        free(r);
        return 0;
    }
	r->map3 = r->map + r->map_w * r->map_h * 2;
	setTable(r);
	r->map1 = r->map;
	r->map2 = r->map + r->map_h*r->map_w;
	r->stat = 1;
    r->point = 16;
        
	return (void*) r;
}

void waterrippletv_free(void *ptr) {
    ripple_tv *r = (ripple_tv*) ptr;
	free(r->ripple_data[0]);
	free(r->map);
	free(r->vtable);
    free(r);
}


static inline void drop(ripple_tv *r, int power)
{
	int x, y;
	int *p, *q;
	x = wfastrand(r)%(r->map_w-4)+2;
	y = wfastrand(r)%(r->map_h-4)+2;
	p = r->map1 + y*r->map_w + x;
	q = r->map2 + y*r->map_w + x;
	*p = power;
	*q = power;
	*(p-r->map_w) = *(p-1) = *(p+1) = *(p+r->map_w) = power/2;
	*(p-r->map_w-1) = *(p-r->map_w+1) = *(p+r->map_w-1) = *(p+r->map_w+1) = power/4;
	*(q-r->map_w) = *(q-1) = *(q+1) = *(q+r->map_w) = power/2;
	*(q-r->map_w-1) = *(q-r->map_w+1) = *(q+r->map_w-1) = *(p+r->map_w+1) = power/4;
}

static void raindrop(ripple_tv *r)
{
	static int period = 0;
	static int rain_stat = 0;
	static unsigned int drop_prob = 0;
	static int drop_prob_increment = 0;
	static int drops_per_frame_max = 0;
	static int drops_per_frame = 0;
	static int drop_power = 0;

	int i;

	if(period == 0) {
		switch(rain_stat) {
		case 0:
			period = (wfastrand(r)>>23)+100;
			drop_prob = 0;
			drop_prob_increment = 0x00ffffff/period;
			drop_power = (-(wfastrand(r)>>28)-2)<<r->point;
			drops_per_frame_max = 2<<(wfastrand(r)>>30); // 2,4,8 or 16
			rain_stat = 1;
			break;
		case 1:
			drop_prob = 0x00ffffff;
			drops_per_frame = 1;
			drop_prob_increment = 1;
			period = (drops_per_frame_max - 1) * 16;
			rain_stat = 2;
			break;
		case 2:
			period = (wfastrand(r)>>22)+1000;
			drop_prob_increment = 0;
			rain_stat = 3;
			break;
		case 3:
			period = (drops_per_frame_max - 1) * 16;
			drop_prob_increment = -1;
			rain_stat = 4;
			break;
		case 4:
			period = (wfastrand(r)>>24)+60;
			drop_prob_increment = -(drop_prob/period);
			rain_stat = 5;
			break;
		case 5:
		default:
			period = (wfastrand(r)>>23)+500;
			drop_prob = 0;
			rain_stat = 0;
			break;
		}
	}
	switch(rain_stat) {
	default:
	case 0:
		break;
	case 1:
	case 5:
		if((wfastrand(r)>>8)<drop_prob) {
			drop(r,drop_power);
		}
		drop_prob += drop_prob_increment;
		break;
	case 2:
	case 3:
	case 4:
		for(i=drops_per_frame/16; i>0; i--) {
			drop(r,drop_power);
		}
		drops_per_frame += drop_prob_increment;
		break;
	}
	period--;
}

void	waterrippletv_apply(void *ptr, VJFrame *frame, int *args)
{
	int x, y, i;
	int dx, dy;
	int h, v;
	int wi, hi;
	int *p, *q, *r;
	signed char *vp;
	uint8_t *src,*dest;
	const int len = frame->len;
    int fresh_rate = args[0];
    int loopnum = args[1];
    int decay = args[2];
    ripple_tv *rip = (ripple_tv*) ptr;

	if(rip->last_fresh_rate != fresh_rate || rip->tick > fresh_rate)
	{
		rip->last_fresh_rate = fresh_rate;
		veejay_memset( rip->map, 0, (rip->map_h*rip->map_w*2*sizeof(int)));
		rip->tick = 0;
	}

	rip->tick ++;
	veejay_memcpy ( rip->ripple_data[0], frame->data[0],len);

	dest = frame->data[0];
	src = rip->ripple_data[0];

	/* impact from the motion or rain drop */
	raindrop(rip);

	/* simulate surface wave */
	wi = rip->map_w;
	hi = rip->map_h;
	
	/* This function is called only 30 times per second. To increase a speed
	 * of wave, iterates this loop several times. */
	for(i=loopnum; i>0; i--) {
		/* wave simulation */
		p = rip->map1 + wi + 1;
		q = rip->map2 + wi + 1;
		r = rip->map3 + wi + 1;
		for(y=hi-2; y>0; y--) {
#pragma omp simd
			for(x=wi-2; x>0; x--) {
				h = *(p-wi-1) + *(p-wi+1) + *(p+wi-1) + *(p+wi+1)
				  + *(p-wi) + *(p-1) + *(p+1) + *(p+wi) - (*p)*9;
				h = h >> 3;
				v = *p - *q;
				v += h - (v >> decay);
				*r = v + *p;
				p++;
				q++;
				r++;
			}
			p += 2;
			q += 2;
			r += 2;
		}

		/* low pass filter */
		p = rip->map3 + wi + 1;
		q = rip->map2 + wi + 1;
		for(y=hi-2; y>0; y--) {
#pragma omp simd
			for(x=wi-2; x>0; x--) {
				h = *(p-wi) + *(p-1) + *(p+1) + *(p+wi) + (*p)*60;
				*q = h >> 6;
				p++;
				q++;
			}
			p+=2;
			q+=2;
		}

		p = rip->map1;
		rip->map1 = rip->map2;
		rip->map2 = p;
	}

	vp = rip->vtable;
	p = rip->map1;
	for(y=hi-1; y>0; y--) {
		for(x=wi-1; x>0; x--) {
			/* difference of the height between two voxel. They are twiced to
			 * emphasise the wave. */
			vp[0] = rip->sqrtable[((p[0] - p[1])>>(rip->point-1))&0xff]; 
			vp[1] = rip->sqrtable[((p[0] - p[wi])>>(rip->point-1))&0xff]; 
			p++;
			vp+=2;
		}
		p++;
		vp+=2;
	}

	hi = frame->height;
	wi = frame->width;
	vp = rip->vtable;

/*	dest2 = dest;
        p = map1;
        for(y=0; y<hi; y+=2) {
                for(x=0; x<wi; x+=2) {
                        h = (p[0]>>(point-5))+128;
                        if(h < 0) h = 0;
                        if(h > 255) h = 255;
                        dest[0] = h;
                        dest[1] = h;
                        dest[wi] = h;
                        dest[wi+1] = h;
                        p++;
                        dest+=2;
                        vp+=2;
                }
                dest += width;
                vp += 2;
                p++;
        }

*/
	 
	
	for(y=0; y<hi; y+=2) {
		for(x=0; x<wi; x+=2) {
			h = (int)vp[0];
			v = (int)vp[1];
			dx = x + h;
			dy = y + v;
			if(dx<0) dx=0;
			if(dy<0) dy=0;
			if(dx>=wi) dx=wi-1;
			if(dy>=hi) dy=hi-1;
			dest[0] = src[dy*wi+dx];

			i = dx;

			dx = x + 1 + (h+(int)vp[2])/2;
			if(dx<0) dx=0;
			if(dx>=wi) dx=wi-1;
			dest[1] = src[dy*wi+dx];

			dy = y + 1 + (v+(int)vp[rip->map_w*2+1])/2;
			if(dy<0) dy=0;
			if(dy>=hi) dy=hi-1;
			dest[wi] = src[dy*wi+i];

			dest[wi+1] = src[dy*wi+dx];
			dest+=2;
			vp+=2;
		}
		dest += wi;
		vp += 2;
	}
}
