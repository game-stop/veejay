#ifndef UTIL_H
#define UTIL_H
/* veejay - Linux VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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

char    *get_str_vevo( void *port, const char *key );
double  *get_dbl_arr_vevo( void *port, const char *key );
void    clone_prop_vevo( void *port, void *to_port, const char *key, const char *as_key );
void    util_convertrgba32( uint8_t **data, int w, int h,int in_pix_fmt,int shiftv, void *out_buffer );
void    util_convertsrc( void *indata, int w, int h, int out_pix_fmt, uint8_t **data);
#endif
