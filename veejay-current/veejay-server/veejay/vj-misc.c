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
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
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
		char *tmp = strdup( node + len + 1);
		return tmp;
	}
	return strdup(node);
}

static int	is_usable_file( filelist_t *filelist, const char *node, const char *filename )
{
	if(!node) 
		return 0;

	struct stat l;
	veejay_memset(&l,0,sizeof(struct stat));
#ifdef STRICT_CHECKING
	assert( filelist != NULL );
	assert( filelist->num_files >= 0 || filelist->num_files < 1024 );
#endif
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
#ifdef STRICT_CHECKING
		veejay_msg(0, "Strange, current working directory seems to be invalid?");
#endif
		return NULL;
	}

	filelist_t *fl = (filelist_t*) vj_malloc(sizeof(filelist_t));
	fl->files      = (char**) vj_calloc(sizeof(char*) * 1024 ); 
	fl->max_files  = 1024;
	fl->num_files  = 0;	
	fl->working_dir = strdup(working_dir);

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
	static void *bg_sampler = NULL;
	if( pass == 0 ) {
		if(frame->ssm = 1 ) {
			n += vj_effect_prepare( frame, VJ_VIDEO_EFFECT_CHAMBLEND );
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_CHAMELEON );		
			return 1;
		}	
		if(frame->ssm == 0) {
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_BGSUBTRACT );
			n += vj_effect_prepare( frame, VJ_VIDEO_EFFECT_DIFF );
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_MOTIONMAP );	
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_CONTOUR );
			n += vj_effect_prepare( frame, VJ_VIDEO_EFFECT_TEXMAP);
		}

		if( frame->ssm == 0 ) 
		{
			//@ supersample
			if(!bg_sampler)
				bg_sampler = subsample_init( frame->width );
			chroma_supersample( info->settings->sample_mode,bg_sampler,frame->data,frame->width,frame->height);
			n += vj_effect_prepare( frame, VJ_VIDEO_EFFECT_CHAMBLEND );
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_CHAMELEON );
			frame->ssm = 1;
			return 1;
		}	
		return 0;
	} else {
	
		if(frame->ssm == 0) {
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_BGSUBTRACT );
			n += vj_effect_prepare( frame, VJ_VIDEO_EFFECT_DIFF );
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_MOTIONMAP );	
			n += vj_effect_prepare( frame, VJ_IMAGE_EFFECT_CONTOUR );
			n += vj_effect_prepare( frame, VJ_VIDEO_EFFECT_TEXMAP);
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
	char c,sign=0,tmp[66];
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
#ifdef STRICT_CHECKING
	assert( size > 0 );
#endif
	int num,flags,base;
	char *str, *end, c;
	const char *s;
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


/* performer: run a function in parallel on multi core cpu
 *            move this to a new file.
 *
 */
/*

#define THREAD_STACK 1024*1024

void	performer_init()
{
	mlockall( MCL_CURRENT | MCL_FUTURE );
	stack = malloc(THREAD_STACK);
	pthread_attr_setstack( &attr, stack, THREAD_STACK );
}
*/

struct task
{
        int     task_id;
 	    void	*data;
	    void	*(*handler)(void *arg);
      //  performer_job_routine handler;
		struct  task    *next;
};

typedef struct {
	performer_job_routine	job;
	void			*arg;
} pjob_t;

#define	MAX_WORKERS 16

static int	total_tasks_	=	0;
static int tasks_done[MAX_WORKERS];
static int tasks_todo = 0;
static int exitFlag = 0;
static int taskLock = 0;
static pthread_mutex_t	queue_mutex;//	= PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP; //PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
//pthread_mutex_t sync_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static pthread_cond_t 	tasks_completed;
static pthread_cond_t	current_task;//	= PTHREAD_COND_INITIALIZER;
static	int	numThreads	= 0;
struct	task	*tasks_		= NULL;
struct	task	*tail_task_	= NULL;
 
static pthread_t p_threads[MAX_WORKERS];
static pthread_attr_t p_attr[MAX_WORKERS];
static int	 p_tasks[MAX_WORKERS];
static int	 thr_id[MAX_WORKERS];
static	pjob_t *job_list[MAX_WORKERS];

int	task_get_workers()
{
	return numThreads;
}

void	*task_add(int task_no, void *(fp)(void *data), void *data)
{
	struct	task	*enqueue_task = (struct task*) malloc( sizeof(struct task));
	if(!enqueue_task) {
		return NULL;
	}


//	int err					=	pthread_mutex_lock( &queue_mutex );

	enqueue_task->task_id	=	task_no;
	enqueue_task->handler	=	fp;
	enqueue_task->data		= 	data;
	enqueue_task->next		= 	NULL;

	if( total_tasks_ == 0 ) {
		tasks_				=	enqueue_task;
		tail_task_			=	tasks_;
	}
	else {
		tail_task_->next	=	enqueue_task;
		tail_task_			=	enqueue_task;
	}

	total_tasks_ ++;

	int err						=	pthread_cond_signal( &current_task );
//	err						=	pthread_mutex_unlock( &queue_mutex );


}

struct	task	*task_get( pthread_mutex_t *mutex )
{
	int err = pthread_mutex_lock(mutex);
	struct task *t = NULL;
	if( total_tasks_ > 0  ) {
		t 		=	tasks_;
		tasks_	=	tasks_->next;

		if( tasks_ == NULL ) {
			tail_task_ = NULL;
		}

		total_tasks_ --;
	}

	err = pthread_mutex_unlock(mutex);
 
	return t;
}

void		task_run( struct task *task, void *data, int id)
{
	if( task )
	{
		(*task->handler)(data);
		
		pthread_mutex_lock(&queue_mutex);
		tasks_done[id] ++; //@ inc, it is possible same thread tasks both tasks
		pthread_cond_signal( &tasks_completed );
		pthread_mutex_unlock( &queue_mutex );

	}
}

void		*task_thread(void *data)
{
	const unsigned int id = (int) (int*) data;
	for( ;; ) 
	{
		pthread_mutex_lock( &queue_mutex );
		while( total_tasks_ == 0 ) {
			if( exitFlag ) {
				pthread_mutex_unlock(&queue_mutex);
				pthread_exit(0);
				return NULL;
			}
			pthread_cond_wait(&current_task, &queue_mutex );
		}
		pthread_mutex_unlock( &queue_mutex );
		
		struct task *t = task_get( &queue_mutex );

		if( t ) {
			task_run( t, t->data, id );
			free(t);
			t = NULL;
		}
	}
}

int			n_cpu = 1;

void	task_free()
{
	int i;
	for ( i = 0; i < MAX_WORKERS; i ++ ) {
		free(job_list[i]);
	}
}

void		task_init()
{
	int i;

	memset( &thr_id, 0,sizeof(thr_id));
	memset( &p_threads,0,sizeof(p_threads));
	memset( &p_tasks, 0,sizeof(p_tasks));

	memset( job_list, 0,sizeof(pjob_t*) * MAX_WORKERS );
	for( i = 0; i < MAX_WORKERS; i ++ ) {
		job_list[i] = vj_malloc(sizeof(pjob_t));
		memset( job_list[i], 0, sizeof(pjob_t));
	}	

	n_cpu = sysconf( _SC_NPROCESSORS_ONLN );
	if( n_cpu <= 0 )
			n_cpu = 1;

	numThreads = 0;
}

int		task_num_cpus()
{
	return n_cpu;
}

int		task_start(int max_workers)
{
	int i;
	if( max_workers >= MAX_WORKERS ) {
		veejay_msg(0, "Maximum number of threads is %d", MAX_WORKERS );
		return 0;
	}
	exitFlag = 0;

    /*int max_p = sched_get_priority_max( SCHED_FIFO );
    int min_p = sched_get_priority_min( SCHED_FIFO );

    max_p = (int) ( ((float) max_p) * 0.8f );
    if( max_p < min_p )
	    max_p = min_p;
	*/

    struct sched_param param;
	cpu_set_t cpuset;
	pthread_cond_init( &tasks_completed, NULL );
	pthread_cond_init( &current_task, NULL );
	pthread_mutex_init( &queue_mutex , NULL);
	pthread_mutex_lock( &queue_mutex );
	for( i = 0 ; i < max_workers; i ++ ) {
		thr_id[i]	= i;
		pthread_attr_init( &p_attr[i] );
//		pthread_attr_setstacksize( &p_attr[i], 4096 );
	
//	  	pthread_attr_setschedpolicy( &p_attr[i], SCHED_FIFO );
  //  		pthread_attr_setschedparam( &p_attr[i], &param );



		if( n_cpu > 1 ) {
			CPU_ZERO(&cpuset);
			CPU_SET( ((i+1) % n_cpu ), &cpuset );
			if(pthread_attr_setaffinity_np( &p_attr[i], sizeof(cpuset), &cpuset ) != 0 )
				veejay_msg(0,"Unable to set CPU %d affinity to thread %d", ((i+1)%n_cpu),i);
		}

		if( pthread_create(  &p_threads[i], (void*) &p_attr[i], task_thread, i ) )
		{
			veejay_msg(0, "%s: error starting thread %d/%d", __FUNCTION__,i,max_workers );
			
			memset( &p_threads[i], 0, sizeof(pthread_t) );
			return -1;
		}
	}

	numThreads = max_workers;

	pthread_mutex_unlock( &queue_mutex );

	return numThreads;
}

void		task_stop(int max_workers)
{
	int i;
	
	pthread_mutex_lock(&queue_mutex);
	exitFlag = 1;
	pthread_cond_broadcast( &current_task );
	pthread_mutex_unlock(&queue_mutex);

	for( i = 0; i < max_workers;i++ ) {
		pthread_join( p_threads[i], (void*)&p_tasks[i] );
		pthread_attr_destroy( &p_attr[i] );
	}
	
	pthread_mutex_destroy( &queue_mutex );
	pthread_cond_destroy( &tasks_completed );
	pthread_cond_destroy( &current_task );	

	task_init();
	
}

void	task_wait_all()
{

}

void	performer_job( int n )
{
	int i;

	pthread_mutex_lock(&queue_mutex);
//	taskLock = 1;
//	pthread_mutex_unlock(&queue_mutex);
	
	tasks_todo = n;
	veejay_memset( tasks_done, 0, sizeof(tasks_done));

	for( i = 0; i < n; i ++ ) {
		pjob_t *slot  = job_list[i];
		task_add( i, slot->job, slot->arg );
	}

	//task_ready(n);

	pthread_mutex_unlock( &queue_mutex );

/*	for( i = 0; i < n; i ++ ) {
		void *exit_code = NULL;
		pjob_t *slot = &(arr[i]);
		pthread_join( slot->thread, NULL );
		if( exit_code != 0 ) {

		}
	} */

	int stop = 0;
	int c = 0;
	while(!stop) {
		pthread_mutex_lock( &queue_mutex );
		int done = 0;
		for( i = 0 ; i < tasks_todo; i ++ ) {
			done += tasks_done[i];
		}

		
		if( done < tasks_todo ) {
			pthread_cond_wait( &tasks_completed, &queue_mutex );
			done = 0;
			for( i = 0 ; i < tasks_todo; i ++ ) {
				done += tasks_done[i];
			}
		}
		
		if( done == tasks_todo )
		{
			stop = 1;
		}

		pthread_mutex_unlock(&queue_mutex);
	}

}

void	performer_set_job( int num, performer_job_routine job , void *arg )
{
#ifdef STRICT_CHECKING
	assert( num >= 0 && num < MAX_WORKERS );
#endif
	job_list[ num ]->job = job;
	job_list[ num ]->arg = arg;

}

void	performer_new_job( int n_jobs )
{	
	int i;
//	for( i = 0; i < n_jobs; i ++ )
//		veejay_memset( job_list[i], 0, sizeof(pjob_t) );

}


