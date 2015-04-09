/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nwelburg@gmail.com>
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
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <libel/vj-mmap.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>

#define PADDED(a,m) ( a > 0 ? (a / m->page_size) * m->page_size  : 0)

void		mmap_free(mmap_region_t *map)
{
	if(map)
	{
		if(map->map_start) 
			munmap_file(map);
		free(map);
	}
	map = NULL;
}

mmap_region_t *	mmap_file(int fd, long offset, long length, int fs)
{
	mmap_region_t *map = (mmap_region_t*) vj_malloc(sizeof( mmap_region_t ));
	veejay_memset( map, 0, sizeof( mmap_region_t ));

	map->fd		= fd;
	map->page_size  = getpagesize();
	map->map_length = length;
	map->map_start  = NULL;
	map->eof = fs;
	map->mem_offset = offset;
	remap_file( map, offset );

	veejay_msg(VEEJAY_MSG_DEBUG, "\tmemory map region is %f Mb",( (float) length / 1048576.0f ) );
	return map;
}


int	is_mapped( mmap_region_t *map, long offset, long size )
{
	// check if memory is in mapped region
	off_t real_offset = PADDED( offset, map );

	long rel_o = (map->mem_offset > 0 ? offset - map->mem_offset : offset );

	if( (rel_o + size) > map->map_length )
	{
		return 0;
	}

	if( real_offset >= map->start_region && 
		real_offset + size <= map->end_region )
		return 1;

	return 0;
}

int	remap_file( mmap_region_t *map, long offset )
{
	size_t padding = offset % map->page_size;
	size_t new_length = map->map_length;
	size_t real_length = 0;
	off_t	real_offset = PADDED( offset, map );
	
	real_length = (padding + new_length);
        if( real_length > map->eof )
	{
		real_length = PADDED(map->eof ,map);
	}
	if(map->map_start != NULL)
	{
		munmap_file( map );
	}

	map->mem_offset = offset;
	map->map_start = mmap( 0, real_length, PROT_READ, MAP_SHARED, map->fd, real_offset );
	if( map->map_start == MAP_FAILED)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to map memory: %s", strerror(errno));
		return 0;
	}

	map->data_start = map->map_start + padding;

	map->start_region = real_offset;
	map->end_region   = real_length + real_offset;

	return 1;
}

int	munmap_file( mmap_region_t *map )
{
	if(map->map_start == NULL)
		return 1;

	int n = munmap( map->map_start, map->map_length );
	if(n==-1)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Unable to unmap memory: %s",strerror(errno));
	}
	map->map_start = NULL;
	return n;
}

long	mmap_read( mmap_region_t *map,long offset, long bytes, uint8_t *buf )
{
	if( !is_mapped( map, offset, bytes ))
	{
		if(remap_file( map, offset ) == 0 ) {
			return -1;
		}
		if(!is_mapped(map, offset, bytes)) {
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to map %ld bytes from position %ld" , bytes, offset );
			return -1;
		}
	}

	long rel_offset = (map->mem_offset > 0 ? offset - map->mem_offset : offset );

	uint8_t *d1 = map->data_start + rel_offset;

	veejay_memcpy( buf, d1, bytes );
	
	return bytes;
}
