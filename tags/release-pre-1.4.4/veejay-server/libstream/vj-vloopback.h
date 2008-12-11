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



void *vj_vloopback_open(const char *device_name, int norm, int mode,
	int w, int h, int pixel_format);
// if using write mode
int	vj_vloopback_start_pipe( void *vloop );
int	vj_vloopback_write_pipe( void *vloop );

// resfresh buffer every cycle
int	vj_vloopback_fill_buffer( void *vloop, uint8_t **image );

// if using mmap mode
//int	vj_vloopback_start_mmap( void *vloop );

//int	vj_vloopback_ioctl( void *vloop, unsigned long int cmd, void *arg );
void	vj_vlooopback_close( void *vloop );

int	vj_vloopback_get_mode( void *vloop );
void     vj_vloopback_close( void *vloop );


//void	vj_vloopback_signal_handler( void *vloop, int sig_no );

