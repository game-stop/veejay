/* 
 * Linux VeeJay
 *
 * Copyright(C)2010-2011 Niels Elburg <nwelburg@gmail.com / niels@dyne.org >
 *             - re-use Freej's v4l2 cam driver
 *             - implemented controls method and image format negotiation  
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
#ifndef V4L2UTILS
#define V4L2UTILS
typedef struct {
	char 	*file;
	int	channel;
	int	host_fmt;
	int	wid;
	int	hei;
	float 	fps;
	char	norm;
	void	*v4l2;
	VJFrame	*dst;
	int	paused;
	int	stop;
	int	grabbing;
	int	retries;
	pthread_mutex_t mutex;
	pthread_t	thread;
	pthread_attr_t	attr;
	pthread_cond_t	cond;
} v4l2_thread_info;

void 	*v4l2open ( const char *file, const int input_channel, int host_fmt, int wid, int hei, float fps, char norm );
int		v4l2_pull_frame(void *vv,VJFrame *dst);
void	v4l2_close( void *d );
const char* v4l2_get_property_name( const int id );
void	v4l2_get_controls( void *d, void *port );
void	v4l2_set_control( void *v, int32_t type,  int32_t value );
int32_t	v4l2_get_control( void *d, int32_t type );
int		v4l2_set_roi( void *d, int w, int h, int x, int y );
int		v4l2_reset_roi( void *d );
int		v4l2_get_currentscaling_factor_and_pixel_aspect(void *d);
int		v4l2_num_devices();
char	**v4l2_get_device_list();
int		v4l2_get_composite_status( void *d );
void	v4l2_set_composite_status( void *d, int status);
void	v4l2_set_input_channel( void *d, int num );
void	v4l2_set_brightness( void *d, int32_t value );
int32_t v4l2_get_brightness( void *d );
void	v4l2_set_saturation( void *d, int32_t value );
int32_t	v4l2_get_saturation( void *d );
void	v4l2_set_temperature( void *d, int32_t value );
int32_t	v4l2_get_temperature( void *d );
void	v4l2_set_contrast( void *d, int32_t value );
int32_t v4l2_get_contrast( void *d );
void	v4l2_set_hue( void *d, int32_t value );
int32_t	v4l2_get_hue( void *d );
VJFrame	*v4l2_get_dst( void *v,uint8_t *Y, uint8_t *U, uint8_t *V );
int	v4l2_poll( void *d , int nfds, int timeout );
int	v4l2_thread_start( v4l2_thread_info *info );
void	v4l2_thread_stop( v4l2_thread_info *i );
int	v4l2_thread_pull( v4l2_thread_info *i , VJFrame *dst );
void	*v4l2_thread_new( char *file, int channel, int host_fmt, int wid, int hei, float fps, char norm );
v4l2_thread_info *v4l2_thread_info_get( void *vv);
void	v4l2_set_status( void *d , int status);
void	v4l2_thread_set_status( v4l2_thread_info *i, int status );
#endif

