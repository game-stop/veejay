/*
 * Copyright (C) 2002-2006 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

/*
	Cache frames from file to memory
 */
#include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libel/elcache.h>

typedef struct
{
	int	size;
	int	fmt;
	long	num;
	void    *buffer;
} cache_slot_t;

typedef struct
{
	cache_slot_t	**cache;
	int		len;
	long		*index;
} cache_t;

static	int	cache_free_slot(cache_t *v)
{
	int i;
	for( i = 0; i < v->len; i ++ )
	 if( v->index[i] == -1 ) return i;
	return -1;
}

static	long	total_mem_used_ = 0;

static	void	cache_claim_slot(cache_t *v, int free_slot, uint8_t *linbuf, long frame_num,int buf_len,int format)
{
	// create new node
	cache_slot_t *data = (cache_slot_t*) vj_malloc(sizeof(cache_slot_t));
	data->size = buf_len;
	data->num  = frame_num;
	data->fmt  = format;
	data->buffer = vj_malloc( buf_len );
	// clear old buffer
	if( v->index[free_slot] >= 0 )
	{
		cache_slot_t *del_slot = v->cache[free_slot];
		total_mem_used_ -= del_slot->size;
		free( del_slot->buffer );
		free( del_slot );
		v->cache[free_slot] = NULL;
	}

	veejay_memcpy( data->buffer, linbuf, buf_len );
	v->index[ free_slot ] = frame_num;
	v->cache[ free_slot ] = data;
	total_mem_used_ += buf_len;
}

static	int	cache_find_slot( cache_t *v, long frame_num )
{
	int i;
	int k = 0;
	long n = 0;
	for( i = 0; i < v->len ; i ++ )
	{
		long d = abs( v->index[i] - frame_num );
		if( d > n )
		{	n = d;	k = i ; }
	}
	return k;
}

static	int	cache_locate_slot( cache_t *v, long frame_num)
{
	int i;
	for( i = 0; i < v->len ; i ++ )
		if( v->index[i] == frame_num ) 
			return i;
	return -1;
}


void	*init_cache( unsigned int n_slots )
{
	if(n_slots <= 0)
		return NULL;
	cache_t *v = (cache_t*) vj_calloc(sizeof(cache_t));
	v->len = n_slots;
	v->cache = (cache_slot_t**) vj_calloc(sizeof(cache_slot_t*) * v->len );
	if(!v->cache)
	{
		free(v);
		return NULL;
	}
	v->index = (long*) vj_malloc(sizeof(long) * v->len );
	int n;
	for( n = 0; n < v->len ; n ++ )
		v->index[n] = -1;
	return (void*) v;
}

void	reset_cache(void *cache)
{
	int i = 0;
	cache_t *v = (cache_t*) cache;

	for( i = 0; i < v->len; i ++ )
	{
		v->index[i] = -1;
		if( v->cache[i] )
		{
			total_mem_used_ -= v->cache[i]->size;
			if(v->cache[i]->buffer)
			 free(v->cache[i]->buffer);
			free(v->cache[i]);
			v->cache[i] = NULL;
		}
	}
}

long	cache_avail_mb()
{
	return ( total_mem_used_ == 0 ? 0 : total_mem_used_ / (1024 * 1024 ));
}


void	free_cache(void *cache)
{
	cache_t *v = (cache_t*) cache;	
	if(v->cache) {
		free(v->cache);
		v->cache = NULL;
	}
	if(v->index) {
		free(v->index);
		v->index = NULL;
	}
	if(v) {
		free(v);
	}
	v = NULL;
}

void	cache_frame( void *cache, uint8_t *linbuf, int buflen, long frame_num , int format)
{
	cache_t *v = (cache_t*) cache;
	if( buflen <= 0 )
		return;
	int slot_num = cache_free_slot( cache );
	if( slot_num == -1 )
		slot_num = cache_find_slot( v, frame_num );

	cache_claim_slot(v, slot_num, linbuf, frame_num, buflen, format);
} 

uint8_t *get_cached_frame( void *cache, long frame_num, int *buf_len, int *format )
{
	cache_t *v = (cache_t*) cache;
	int slot = cache_locate_slot( v, frame_num );
	if( slot == -1 )
		return NULL;

	cache_slot_t *data = v->cache[ slot ];

	*buf_len 	= data->size;
	*format		= data->fmt;
	return (uint8_t*) data->buffer;
}
