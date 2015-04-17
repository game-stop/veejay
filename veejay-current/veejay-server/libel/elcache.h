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

#ifndef ELCACHE_H
#define ELCACHE_H

uint8_t *get_cached_frame( void *cache, long frame_num, int *buf_len, int *format );
void	cache_frame( void *cache, uint8_t *linbuf, int buflen, long frame_num , int format);
void	free_cache(void *cache);
void	*init_cache( unsigned int n_slots );
void  reset_cache(void *cache);
long   cache_avail_mb(); 

#endif
