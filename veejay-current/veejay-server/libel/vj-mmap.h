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
#ifndef VJ_MMAP_H
#define VJ_MMAP_H
#include <sys/types.h>
#include <sys/mman.h>
#include <stdint.h>
typedef struct
{
	unsigned char  *map_start;	/* result of mmap() */
	unsigned char *data_start;	/* start of data */
	uint64_t	start_region;
	uint64_t	end_region;
	off_t	mem_offset;	/* start of image */
	int	fd;		/* file descriptor */
	size_t	page_size;	/* page size */
	size_t	map_length;	/* requested map size */
	long  eof;		/* file size */
} mmap_region_t;

// map file portion to memory, return mapped region
mmap_region_t *	mmap_file(int fd, long offset, long length, int fs);

// see if requested boundaries is mapped in memory  
int	is_mapped( mmap_region_t *map, long offset, long size );

// remap a portion of a file in memory
int	remap_file( mmap_region_t *map, long offset );

// unmap memory
int	munmap_file( mmap_region_t *map );

void	mmap_free(mmap_region_t *map );

long	mmap_read( mmap_region_t *map, long offset, long bytes, uint8_t *buf);

#endif
