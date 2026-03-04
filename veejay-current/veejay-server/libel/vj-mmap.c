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
#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>

#include <libel/vj-mmap.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>

void mmap_free(mmap_region_t *map)
{
	if (map)
	{
		if (map->map_start)
			munmap_file(map);
		free(map);
	}
}

mmap_region_t *mmap_file(int fd, off_t offset, size_t length, off_t fs)
{
	mmap_region_t *map = (mmap_region_t *)vj_malloc(sizeof(mmap_region_t));
	veejay_memset(map, 0, sizeof(mmap_region_t));

	map->fd = fd;
	map->page_size = getpagesize();
	map->map_length = length;
	map->map_start = NULL;
	map->eof = fs;
	if (!remap_file(map, offset))
	{
		free(map);
		return NULL;
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "\tmemory map region is %.2f Mb", ((float)length / 1048576.0f));
	return map;
}


#define VEEJAY_SMALL_MAP 0 /* map entire file if small */
#define VEEJAY_MEDIUM_MAP (4*1024*1024) /* 4 MB */
#define VEEJAY_LARGE_MAP  (8*1024*1024) /* 16 MB */

size_t mmap_file_suggest_size(const char *filename, off_t *file_size_out)
{
    struct stat st;
    if (stat(filename, &st) != 0) {
        perror("stat failed");
        return 0;
    }

    off_t fsize = st.st_size;
    if (file_size_out)
        *file_size_out = fsize;

    if (fsize <= (4*1024*1024)) {
        return (size_t)fsize;
    } else if (fsize <= (64*1024*1024)) {
        return VEEJAY_MEDIUM_MAP;
    }
	return VEEJAY_LARGE_MAP;
}

int is_mapped(mmap_region_t *map, off_t offset, size_t size)
{
	if (!map || !map->map_start || size == 0 || offset < 0)
		return 0;

	if (offset >= map->eof)
		return 0;

	if ((off_t)size > (map->eof - offset))
		return 0;

	off_t end = offset + (off_t)size;

	return (offset >= map->start_region &&
			end <= map->end_region);
}

int remap_file(mmap_region_t *map, off_t offset)
{
	if (!map || offset < 0)
		return 0;

	off_t real_offset = (offset / map->page_size) * map->page_size;
	size_t padding = (size_t)(offset - real_offset);

	if (real_offset >= map->eof)
		return 0;

	off_t max_available = map->eof - real_offset;
	if (max_available <= 0)
	{
		return 0;
	}

	if (map->map_length > SIZE_MAX - padding)
	{
		return 0;
	}

	size_t real_length = padding + map->map_length;
	if ((off_t)real_length > max_available)
	{
		real_length = (size_t)max_available;
	}

	if (real_length == 0)
	{
		return 0;
	}

	if (map->map_start)
		munmap_file(map);

	int adv = posix_fadvise(map->fd,
							real_offset,
							real_length,
							POSIX_FADV_RANDOM);
	if (adv != 0)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "posix_fadvise(POSIX_FADV_RANDOM) failed: %s", strerror(adv));
	}

	void *ptr = mmap(NULL, real_length, PROT_READ, MAP_SHARED,
					 map->fd, real_offset);

	if (ptr == MAP_FAILED)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to map memory: %s", strerror(errno));
		return 0;
	}

	if (madvise(ptr, real_length, MADV_RANDOM) == -1)
	{
		veejay_msg(VEEJAY_MSG_WARNING,
				   "madvise(MADV_SEQUENTIAL) failed: %s",
				   strerror(errno));
	}

	map->map_start = ptr;
	map->mapped_length = real_length;
	map->data_start = (uint8_t *)ptr + padding;
	map->start_region = real_offset;
	map->end_region = real_offset + real_length;

	return 1;
}

int munmap_file(mmap_region_t *map)
{
	if (!map || !map->map_start)
		return 0;

	int r = munmap(map->map_start, map->mapped_length);
	if (r == -1)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Unable to unmap memory: %s", strerror(errno));
		return -1;
	}

	map->map_start = NULL;
	map->mapped_length = 0;
	return 0;
}

off_t mmap_read(mmap_region_t *map, off_t offset, size_t bytes, uint8_t *buf)
{
	if (!map || !buf || bytes <= 0 || offset < 0)
		return -1;

	if (offset >= map->eof)
		return -1;

	if (bytes > (map->eof - offset))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to read beyond EOF at position %jd", (intmax_t)offset);
		return -1;
	}

	if (!is_mapped(map, offset, bytes))
	{
		if (!remap_file(map, offset))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to remap at position %jd", (intmax_t)offset);
			return -1;
		}

		if (!is_mapped(map, offset, bytes))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Mapping insufficient for %jd bytes at %jd", (intmax_t)bytes, (intmax_t)offset);
			return -1;
		}
	}

	off_t rel_offset = offset - map->start_region;
	uint8_t *src = (uint8_t *)map->map_start + rel_offset;

	veejay_memcpy(buf, src, bytes);

	return bytes;
}
