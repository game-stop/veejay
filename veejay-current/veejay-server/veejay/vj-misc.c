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
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <libsample/sampleadm.h>
#include <libstream/vj-tag.h>
#include <veejay/vj-misc.h>
#include <veejay/vj-lib.h>
#include <veejaycore/vjmem.h>
#include <libvje/internal.h>
#ifdef HAVE_JPEG
#include <veejay/jpegutils.h>
#endif
#include <veejaycore/vj-msg.h>
#include <libvje/vje.h>
#include <veejaycore/yuvconv.h>
#include <libavutil/pixfmt.h>
#include <veejaycore/avcommon.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <pthread.h>
#ifdef USE_GDK_PIXBUF
#include <libel/lav_io.h>
#endif

static unsigned int vj_relative_time = 0;
static unsigned int vj_stamp_ = 0;
long vj_get_timer(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return ((tv.tv_sec & 1000000) + tv.tv_usec);
}

unsigned	int	vj_stamp(void)
{
	vj_stamp_ ++;
	return vj_stamp_;
}

void	vj_stamp_clear(void)
{
	vj_stamp_ = 0;
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

static char *relative_path(filelist_t *filelist, const char *node)
{
    int len = strlen(filelist->working_dir);
    
    if (strncmp(node, filelist->working_dir, len) == 0) {
        const char *rel = node + len;
        if (*rel == '/' || *rel == '\\') {
            rel++;
        }
        return vj_strdup(rel);
    }

    return vj_strdup(node);
}

static int is_usable_file(filelist_t *filelist, const char *node, const char *filename)
{
    if (!node)
        return 0;

    struct stat st;
    veejay_memset(&st, 0, sizeof(struct stat));

    if (lstat(node, &st) < 0)
        return 0;

    // Handle symlink
    if (S_ISLNK(st.st_mode)) {
        veejay_memset(&st, 0, sizeof(struct stat));
        if (stat(node, &st) < 0)
            return 0;
        return 1;
    }

    // Directory
    if (S_ISDIR(st.st_mode))
        return 1;

    // Regular file
    if (S_ISREG(st.st_mode)) {
        if (is_it_usable(node)
#ifdef USE_GDK_PIX_BUF
            || lav_is_supported_image_file(node)
#endif
        ) {
            if (filelist->num_files < filelist->max_files) {
                filelist->files[filelist->num_files] = relative_path(filelist, node);
                filelist->num_files++;
            }
            return 1;
        }
        return 0;
    }

    // Character or block device (e.g., /dev/random, /dev/zero)
    if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
        int fd = open(node, O_RDONLY);
        if (fd >= 0) {
            close(fd);
            return 1;
        }
        return 0;
    }

    // Other special types (FIFO, socket, etc.) → not usable
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

#ifdef HAVE_JPEG
#define MAX_JPEG_BUFFER (65535 * 24)

int vj_perform_screenshot2(veejay_t *info, VJFrame *frame)
{
    FILE *frame_file = NULL;
    int res = 0;
    int jpeg_size = 0;

    uint8_t *jpeg_buff = vj_malloc(MAX_JPEG_BUFFER);
    if (!jpeg_buff) return -1;

    VJFrame tmp;
    vj_get_yuv_template(&tmp,
                        info->video_output_width,
                        info->video_output_height,
                        info->pixel_format);

    tmp.data[0] = vj_malloc(tmp.len * 3);
    if (!tmp.data[0]) {
        free(jpeg_buff);
        return -1;
    }
    tmp.data[1] = tmp.data[0] + tmp.len;
    tmp.data[2] = tmp.data[1] + tmp.len + tmp.uv_len;
    tmp.format = PIX_FMT_YUVJ420P;

    yuv_convert_any_ac(frame, &tmp);

    if (!info->uc->filename) {
        info->uc->filename = vj_malloc(32);
        if (!info->uc->filename) {
            free(tmp.data[0]);
            free(jpeg_buff);
            return -1;
        }
        snprintf(info->uc->filename, 32, "%020lld.jpg", frame->frame_num);
    }

    frame_file = fopen(info->uc->filename, "wb");
    if (frame_file) {
        jpeg_size = encode_jpeg_raw(jpeg_buff, MAX_JPEG_BUFFER, 100,
                                    info->settings->dct_method,
                                    info->current_edit_list->video_inter,
                                    0,
                                    info->video_output_width,
                                    info->video_output_height,
                                    tmp.data[0], tmp.data[1], tmp.data[2]);

        if (jpeg_size > 0) {
            res = fwrite(jpeg_buff, jpeg_size, 1, frame_file);
            if (res)
                veejay_msg(VEEJAY_MSG_INFO, "Wrote video frame %lld to %s", frame->frame_num, info->uc->filename);
        }
        fclose(frame_file);
    }

    free(jpeg_buff);
    free(tmp.data[0]);

    return res;
}


#endif

int	veejay_create_temp_file(const char *prefix, char *dst)
{
    struct timespec now;
    clock_gettime( CLOCK_MONOTONIC, &now );

	sprintf(dst, "%s_%ld_%ld",prefix, now.tv_sec, now.tv_nsec ); 
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

int	verify_working_dir(void)
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
		char tmp[2028];
		snprintf( tmp, sizeof(tmp), "%s/%s", path, files[n]->d_name );
		if( try_file( tmp ) )
			c++;
		free( files[n] );
	}

	free(files);
	return c;
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


int vj_get_sample_display_name(char **destination, const char *filename)
{
    if (!destination || !filename)
        return 0;

    *destination = NULL;

    const char *start = strrchr(filename, '/');
    start = (start) ? start + 1 : filename;

    const char *end = strrchr(start, '.');
    size_t len = (end && end > start) ? (size_t)(end - start)
                                      : strlen(start);

    if (len > 12) {
        *destination = malloc(13);
        if (!*destination)
            return 0;

        memcpy(*destination, start, 8);
        (*destination)[8] = '~';
        memcpy(*destination + 9, start + len - 3, 3);
        (*destination)[12] = '\0';
    } else {
        *destination = strndup(start, len);
        if (!*destination)
            return 0;
    }

    return 1;
}