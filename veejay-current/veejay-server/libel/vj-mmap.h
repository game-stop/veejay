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
    void   *map_start;       /* result of mmap() */
    uint8_t *data_start;     /* pointer to logical data start */

    off_t  start_region;     /* file offset where mapping starts */
    off_t  end_region;       /* file offset where mapping ends */

    off_t  eof;              /* file size */

    int    fd;               /* file descriptor */

    size_t page_size;        /* system page size */
    size_t map_length;       /* requested mapping window */
    size_t mapped_length;    /* actual mapped length */

} mmap_region_t;

// map file portion to memory, return mapped region
mmap_region_t *	mmap_file(int fd, off_t offset, size_t length, off_t fs);

size_t mmap_file_suggest_size(const char *filename, off_t *file_size_out);

// see if requested boundaries is mapped in memory  
int	is_mapped( mmap_region_t *map, off_t offset, size_t size );

// remap a portion of a file in memory
int	remap_file( mmap_region_t *map, off_t offset );

// unmap memory
int	munmap_file( mmap_region_t *map );

void	mmap_free(mmap_region_t *map );

off_t	mmap_read( mmap_region_t *map, off_t offset, size_t bytes, uint8_t *buf);

#endif
