/* veejay - Linux VeeJay
 *           (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef LZOH
#define LZOH
#define LZO_AL(size) (((size) + (sizeof(long) - 1)) / sizeof(long))
void    *lzo_new( int pf, int wid, int hei, int is_decoder );
void	lzo_print_buf( uint8_t *buf, int len );
void	lzo_free( void *lzo );
long    lzo_decompress_el( void *lzo, uint8_t *linbuf, int linbuf_len,uint8_t *dst[4], int width, int height, int pixfmt);
long	lzo_compress_frame( void *lzo, VJFrame *frame, uint8_t *src[4], uint8_t *dst );
#endif
