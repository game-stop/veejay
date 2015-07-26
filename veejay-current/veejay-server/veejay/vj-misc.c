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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/statfs.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-misc.h>
#include <veejay/vj-lib.h>
#include <libvjmem/vjmem.h>
#include <libvje/internal.h>
#ifdef HAVE_JPEG
#include <veejay/jpegutils.h>
#endif
#include <libvjmsg/vj-msg.h>
#include <libvje/vje.h>
#include <libyuv/yuvconv.h>
#include <libavutil/pixfmt.h>
#include <pthread.h>


static unsigned int vj_relative_time = 0;
static unsigned int vj_stamp_ = 0;
long vj_get_timer()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return ((tv.tv_sec & 1000000) + tv.tv_usec);
}

unsigned	int	vj_stamp()
{
	vj_stamp_ ++;
	return vj_stamp_;
}

void	vj_stamp_clear()
{
	vj_stamp_ = 0;
}

long vj_get_relative_time()
{
    long time, relative;
    time = vj_get_timer();
    relative = time - vj_relative_time;
    vj_relative_time = time;
    return relative;
}

static struct
{
	const char *ext;
} filefilters[] =  {
 { ".avi" },
 { ".mov" },
 { ".dv"  },
 { ".edl" },
 { ".y4m" },
 { NULL}
};

static 	int	is_it_usable(const char *file)
{
	int i = 0;
	while ( filefilters[i].ext != NULL ) {
		if(strcasestr( file, filefilters[i].ext ) )
			return 1;
		i++;
	}
	return 0;
}

static	char	*relative_path(filelist_t *filelist, const char *node)
{
	int len = strlen(filelist->working_dir);
	if( node + len + 1 ) {
		char *tmp = vj_strdup( node + len + 1);
		return tmp;
	}
	return vj_strdup(node);
}

static int	is_usable_file( filelist_t *filelist, const char *node, const char *filename )
{
	if(!node) 
		return 0;

	struct stat l;
	veejay_memset(&l,0,sizeof(struct stat));
	if( lstat( node, &l) < 0 )
		return 0;
	
	if( S_ISLNK( l.st_mode )) {
		veejay_memset(&l,0,sizeof(struct stat));
		stat(node, &l);
		return 1;
	}
	
	if( S_ISDIR( l.st_mode ) ) {
		return 1;
	}

	if( S_ISREG( l.st_mode ) ) {
		if( is_it_usable(node)) {
			if( filelist->num_files < filelist->max_files ) {
				filelist->files[ filelist->num_files ] =
					relative_path(filelist,node);
				filelist->num_files ++;
			}
		}
	}
	return 0;
}

static int	dir_selector( const struct dirent *dir )
{	
	return 1;
}

static int	find_files( filelist_t *filelist, const char *path )
{
	struct dirent **files = NULL;
	int N = scandir ( path, &files, dir_selector,alphasort );
	int n;
	if( N < 0 ) {
		return 0;
	}

	for( n = 0; n < N; n ++ )
	{
		char tmp[PATH_MAX];
		if( strcmp( files[n]->d_name, "." ) == 0 ||
		    strcmp( files[n]->d_name, ".." ) == 0 ) {
			continue;
		}

		snprintf(tmp,sizeof(tmp), "%s/%s", path,files[n]->d_name );
		if(is_usable_file( filelist, tmp, files[n]->d_name ) ) { //@recurse
			find_files( filelist, tmp );
		}
	}

	for( n = 0; n < N; n ++ )  {
		if( files[n])
	 	 free(files[n]);
	}
	free(files);
	return 1;
}
void	free_media_files( veejay_t *info, filelist_t *fl )
{
	int n;
	for( n = 0; n < fl->num_files ; n ++ ) {
		if(fl->files[n] != NULL ) {
			free(fl->files[n]);
		}
	}
	free(fl->files);
	free(fl->working_dir);
	free(fl);
	fl = NULL;
}

filelist_t *find_media_files( veejay_t *info )
{
	char working_dir[PATH_MAX];
	char *wd = getcwd( working_dir, sizeof(working_dir));

	if( wd == NULL ) {
		return NULL;
	}

	filelist_t *fl = (filelist_t*) vj_malloc(sizeof(filelist_t));
	fl->files      = (char**) vj_calloc(sizeof(char*) * 1024 ); 
	fl->max_files  = 1024;
	fl->num_files  = 0;	
	fl->working_dir = vj_strdup(working_dir);

	int res = find_files( fl, wd );

	if( res == 0 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "No files found in %s", wd );
		free( fl->files );
		free( fl->working_dir );
		free( fl );
		fl = NULL;
	}

	return fl;
}


int vj_perform_take_bg(veejay_t *info, VJFrame *frame, int pass)
{
	int n = 0;
	if( pass == 0 ) {
		if(frame->ssm == 1 ) {
			n += vj_effect_prepare( frame, VJ_VIDEO_EFFECT_CHAMBLEND );
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_CHAMELEON );
			if(n > 0 )
				return 1;
		}	
		if(frame->ssm == 0) {
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_BGSUBTRACT );
			n += vj_effect_prepare( frame, VJ_VIDEO_EFFECT_DIFF );
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_MOTIONMAP );	
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_CONTOUR );
			if( n > 0 )
				return 1;
		}

		if( frame->ssm == 0 ) 
		{
			chroma_supersample( info->settings->sample_mode,frame,frame->data );
			n += vj_effect_prepare( frame, VJ_VIDEO_EFFECT_CHAMBLEND );
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_CHAMELEON );
			frame->ssm = 1;
			return 0;
		}

		return (n > 0 ? 0: 1 );
	} else {
		if(frame->ssm == 0) {
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_BGSUBTRACT );
			n += vj_effect_prepare( frame, VJ_VIDEO_EFFECT_DIFF );
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_MOTIONMAP );	
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_CONTOUR );
			return 0;
		}
	}
	return 0;
}

#ifdef HAVE_JPEG
int vj_perform_screenshot2(veejay_t * info, uint8_t ** src)
{
    FILE *frame;
    int res = 0;
    uint8_t *jpeg_buff;
    VJFrame tmp;
    int jpeg_size;

    video_playback_setup *settings = info->settings;

    jpeg_buff = (uint8_t *) malloc( 65535 * 10);
    if (!jpeg_buff)
		return -1;

    vj_get_yuv_template( &tmp,
				info->video_output_width,
				info->video_output_height,
				info->pixel_format );

    if( tmp.shift_v == 0 )
    {
	tmp.data[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * tmp.len * 3);
	tmp.data[1] = tmp.data[0] + tmp.len;
	tmp.data[2] = tmp.data[1] + tmp.len + tmp.uv_len;

	tmp.format = PIX_FMT_YUVJ420P;
	
	VJFrame *srci = yuv_yuv_template( src[0],src[1],src[2], info->video_output_width,
					info->video_output_height , PIX_FMT_YUVJ422P);

	yuv_convert_any_ac( srci,&tmp, srci->format, tmp.format );

    	free(srci);
    }
    else
    {
	tmp.data[0] = src[0];
	tmp.data[1] = src[1];
	tmp.data[2] = src[2];
    }	

	if(info->uc->filename == NULL) 
	{
		info->uc->filename = (char*) malloc(sizeof(char) * 12); 
		sprintf(info->uc->filename, "%06d.jpg", info->settings->current_frame_num );
	}
    frame = fopen(info->uc->filename, "wb");

    if (frame)
    {	
    	jpeg_size = encode_jpeg_raw(jpeg_buff, (65535*10), 100,
				settings->dct_method,  
				info->current_edit_list->video_inter,0,
				info->video_output_width,
				info->video_output_height,
				tmp.data[0],
				tmp.data[1], tmp.data[2]);

   	 res = fwrite(jpeg_buff, jpeg_size, 1, frame);
   	 fclose(frame);
    	 if(res) 
		veejay_msg(VEEJAY_MSG_INFO, "Dumped frame to %s", info->uc->filename);
    }

    if (jpeg_buff)
	free(jpeg_buff);

    if( tmp.shift_v == 0 )
    {
	free(tmp.data[0]);
    }

    return res;
}

#endif

int	veejay_create_temp_file(const char *prefix, char *dst)
{
	time_t today_time_;
	struct tm *today;

	today_time_ = time(NULL);
	today = localtime( &today_time_ );
	/* time:
		prefix_01-01-04-hh:mm:ss
           put time in filename, on cp a.o. the creation date
	   will be set to localtime (annoying for users who
	   copy arround files)
	*/

	sprintf(dst,
		"%s_%02d%02d%02d_%02d%02d%02d",		
		prefix,
		today->tm_mday,
		today->tm_mon,
		(today->tm_year%100),
		today->tm_hour,
		today->tm_min,
		today->tm_sec);

	return 1;
}

void	vj_get_rgb_template(VJFrame *src, int w, int h )
{
	src->width = w;
	src->height = h;
	src->uv_width = 0;
	src->uv_height = 0;
	src->format = PIX_FMT_RGB24;
	src->len = w * h * 3;
	src->uv_len = 0;
	src->data[0] = NULL;
	src->data[1] = NULL;
	src->data[2] = NULL;
}

void	vj_get_yuvgrey_template(VJFrame *src, int w, int h)
{
	src->width = w;
	src->uv_width = 0;
	src->height = h;
	src->format = PIX_FMT_GRAY8;
	src->uv_height = 0;
	src->shift_v = 0;
	src->len = w * h;
	src->uv_len = 0;
	src->shift_h = 0;
	src->data[0] = NULL;
	src->data[1] = NULL;
	src->data[2] = NULL;
}

void	vj_get_yuv_template(VJFrame *src, int w, int h, int fmt)
{
	src->width = w;
	src->height = h;
	
	src->format = get_ffmpeg_pixfmt( fmt );

	if(fmt == FMT_420||fmt == FMT_420F)
	{
		src->uv_height = h >> 1;
		src->uv_width = w >> 1;
		src->shift_v = 1;
		src->uv_len = (w*h)/4;
	}
	if(fmt == FMT_422||fmt==FMT_422F)
	{
		src->uv_height = h >> 1;
		src->uv_width = w;
		src->shift_v = 0;
		src->uv_len = src->uv_width * src->uv_height;

	}
	src->len = w * h;
	src->shift_h = 1;
	src->data[0] = NULL;
	src->data[1] = NULL;
	src->data[2] = NULL;

}

void	vj_get_yuv444_template(VJFrame *src, int w, int h)
{
	src->width = w;
	src->uv_width = w;
	src->height = h;
	src->format = PIX_FMT_YUV444P;
	src->uv_height = h;
	src->shift_v = 0;
	src->len = w * h;
	src->uv_len = src->uv_width * src->uv_height;
	src->shift_h = 0;
	src->data[0] = NULL;
	src->data[1] = NULL;
	src->data[2] = NULL;
}
int	available_diskspace(void)
{
	return 1;
}

static int	possible_veejay_file( const char *file )
{
	if( strstr( file , ".edl" ) || strstr( file, ".EDL" ) ||
		strstr( file, ".sl" ) || strstr(file, ".SL" ) ||
		strstr( file, ".cfg" ) || strstr(file, ".CFG" ) ||	
		strstr( file, ".avi" ) || strstr(file, ".mov" ) )
		return 1;
	return 0;
}

static int	try_file( char *path )
{
	struct stat l;
	memset( &l, 0, sizeof( struct stat ) );
	if( lstat(path, &l ) < 0 )
		return 0;
	if( S_ISREG(l.st_mode ))
	{
		if( possible_veejay_file( path ) )
			return 1;
	}
	return 0;
}

int	verify_working_dir()
{
	char path[1024];
	if(getcwd( path, sizeof(path))== NULL )
		return 0;
	struct dirent **files;
	int n = scandir( path, &files, NULL, alphasort );
	if( n <= 0 )
		return 0;

	int c = 0;
	while( n -- ) {
		char tmp[1024];
		snprintf( tmp, sizeof(tmp), "%s/%s", path, files[n]->d_name );
		if( try_file( tmp ) )
			c++;
		free( files[n] );
	}

	free(files);
	return c;
}


int	sufficient_space(int max_size, int nframes)
{
//bogus
	return available_diskspace();
}
#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SPECIAL	32		/* 0x */
#define LARGE	64		/* use 'ABCDEF' instead of 'abcdef' */


/* the sprintf functions below was stolen and stripped from linux 2.4.33
 * from files:
 *  linux/lib/vsprintf.c
 *  asm-i386/div64.h
 */
#ifdef ARCH_X86
#define do_div(n,base) ({ \
	unsigned long __upper, __low, __high, __mod; \
	asm("":"=a" (__low), "=d" (__high):"A" (n)); \
	__upper = __high; \
	if (__high) { \
		__upper = __high % (base); \
		__high = __high / (base); \
	} \
	asm("divl %2":"=a" (__low), "=d" (__mod):"rm" (base), "0" (__low), "1" (__upper)); \
	asm("":"=A" (n):"a" (__low),"d" (__high)); \
	__mod; \
})
#endif
#ifdef ARCH_X86_64
#define do_div(n,base)						\
({								\
	int _res;						\
	_res = ((unsigned long) (n)) % (unsigned) (base);	\
	(n) = ((unsigned long) (n)) / (unsigned) (base);	\
	_res;							\
})
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static char * kern_number(char * buf, char * end, long long num, int base, int type)
{
	char sign=0,tmp[66];
	static const char small_digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	int i=0;
	const char *digits = small_digits;

	if (type & SIGN) {
		if (num < 0) {
			sign = '-';
			num = -num;
		}
	}

	if (num == 0)
		tmp[i++]='0';
	else while (num != 0)
		tmp[i++] = digits[do_div(num,base)];

	if (sign) {
		if (buf <= end)
			*buf = sign;
		++buf;
	}

	while (i-- > 0) {
		if (buf <= end)
			*buf = tmp[i];
		++buf;
	}

	return buf;
}

static	int	kern_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int num,flags,base;
	char *str, *end;
	str = buf;
	end = buf + size - 1;

	if( end < buf - 1 ) {
		end = ((void*)-1);
		size = end - buf + 1;
	}

	for( ; *fmt; ++fmt ) {
		if( *fmt != '%' ) {
			if( str <= end )
				*str = *fmt;
			++str;
			continue;
		}

		flags = 0;
		repeat:
			++ fmt;
			switch( *fmt ) {
				case ' ': flags |= SPACE; goto repeat;
			}

			base = 10;

			switch( *fmt ) {
				case 'd':
					flags |= SIGN;
					break;
			}

			num = va_arg(args, unsigned int);
			if (flags & SIGN)
				num = (signed int) num;

			str = kern_number(str, end, num, base, flags);
	}
	if( str <= end )
		*str = '\0';
	else if ( size > 0 )
		*end = '\0';

	return str-buf;
}

/*
 * veejay_sprintf can only deal with format 'd' ... it is used to produce
 * status lines.
 *
 *
 */

int	veejay_sprintf( char *s, size_t size, const char *format, ... )
{
	va_list arg;
	int done;
	va_start( arg, format );
	done = kern_vsnprintf( s, size, format, arg );
	va_end(arg);
	return done;
}
#else
int	veejay_sprintf( char *s, size_t size, const char *format, ... )
{
	va_list arg;
	int done;
	va_start(arg,format);
	done = vsnprintf( s,size, format, arg );
	return done;
}
#endif

