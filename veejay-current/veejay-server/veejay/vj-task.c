/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2012 Niels Elburg <nwelburg@gmail.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

/*
	Thread pool for blob processing.

	This module will start (N_CPU * 2) threads to divide the workload.
	
	The run method will assign work to the various threads and signal them to start processing.
	The run method will wait until all tasks are done.

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
#include <pthread.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-misc.h>
#include <veejay/vj-lib.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libvje/internal.h>
#include <veejay/vj-task.h>
#include <libvje/vje.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
 
//@ job description
static	vj_task_arg_t *vj_task_args[MAX_WORKERS];

//@ task
struct task
{
	int8_t   task_id;
	void	*data;
	performer_job_routine handler;
	struct  task    *next;
};

//@ job structure
typedef struct {
	performer_job_routine	job;
	void			*arg;
} pjob_t;

//@ no dynamic, static allocation here.
static int	total_tasks_	=	0;
static uint8_t tasks_done[MAX_WORKERS];
static int tasks_todo = 0;
static int exitFlag = 0;
static pthread_mutex_t	queue_mutex;
static pthread_cond_t 	tasks_completed;
static pthread_cond_t	current_task;
static	int	numThreads	= 0;
struct	task	*tasks_		= NULL;
struct	task	*tail_task_	= NULL;
static pthread_t p_threads[MAX_WORKERS];
static int *p_thread_args[MAX_WORKERS];
static pthread_attr_t p_attr[MAX_WORKERS];
static int	 p_tasks[MAX_WORKERS];
static int	 thr_id[MAX_WORKERS];
static	pjob_t *job_list[MAX_WORKERS];
static int	n_cpu = 1;

static struct task running_tasks[MAX_WORKERS];

#define __lock() pthread_mutex_lock(&queue_mutex)
#define __unlock() pthread_mutex_unlock(&queue_mutex)

int	task_get_workers()
{
	return numThreads;
}

static void	task_add(uint8_t task_no, performer_job_routine fp , void *data)
{
	struct task *enqueue_task = &(running_tasks[task_no]);

	enqueue_task->task_id 	=	task_no;
	enqueue_task->handler	=	fp;
	enqueue_task->data	= 	data;
	enqueue_task->next	= 	NULL;

	if( total_tasks_ == 0 ) {
		tasks_		=	enqueue_task;
		tail_task_	=	tasks_;
	}
	else {
		tail_task_->next=	enqueue_task;
		tail_task_	=	enqueue_task;
	}

	total_tasks_ ++;

	pthread_cond_signal( &current_task );
}

struct	task	*task_get()
{
	struct task *t = NULL;
	if( total_tasks_ > 0  ) {
		t 		=	tasks_;
		tasks_	=	tasks_->next;

		if( tasks_ == NULL ) {
			tail_task_ = NULL;
		}

		total_tasks_ --;
	}
	return t;
}

void		task_run( struct task *task, void *data, int id)
{
	if( task )
	{
		(*task->handler)(data);

		__lock();		
		tasks_done[id] ++; 
		pthread_cond_signal( &tasks_completed );
		__unlock();
	}
}

void		*task_thread(void *data)
{
	const int id = (const int) *((int *) data);
	for( ;; ) 
	{
		struct task *t = NULL;
		__lock();
		while( total_tasks_ == 0 ) {
			if( exitFlag ) {
				__unlock();
				pthread_exit(0);
				return NULL;
			}
			pthread_cond_wait(&current_task, &queue_mutex );
		}

		t = task_get();
		__unlock();
	
		if( t ) {
			task_run( t, t->data, id );
			//free(t);
			t = NULL;
		}
	}
}


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
	memset( &vj_task_args,0,sizeof(vj_task_arg_t*) * MAX_WORKERS );
	for( i = 0; i < MAX_WORKERS; i ++ ) {
		job_list[i] = vj_malloc(sizeof(pjob_t));
		vj_task_args[i] = vj_malloc(sizeof(vj_task_arg_t));
		memset( job_list[i], 0, sizeof(pjob_t));
		memset( vj_task_args[i], 0, sizeof(vj_task_arg_t));
		p_thread_args[i] = malloc( sizeof(int) );
		memset( p_thread_args[i], 0, sizeof(int));
		memset( &(running_tasks[i]), 0, sizeof(struct task));
	}

	n_cpu = sysconf( _SC_NPROCESSORS_ONLN );
	if( n_cpu <= 0 )
	{
		n_cpu = 1;
	}

	numThreads = 0;
}

int		task_num_cpus()
{
	return n_cpu;
}

int		task_start(int max_workers)
{
	struct sched_param param;
	int i;
	if( max_workers >= MAX_WORKERS ) {
		veejay_msg(0, "Maximum number of threads is %d", MAX_WORKERS );
		return 0;
	}
	exitFlag = 0;

    	int max_p = sched_get_priority_max( SCHED_FIFO );
    	int min_p = sched_get_priority_min( SCHED_FIFO );

    	max_p = (int) ( ((float) max_p) * 0.90f );
    	if( max_p < min_p )
	    max_p = min_p;
	
	veejay_memset( &param, 0, sizeof(param));

	param.sched_priority =  max_p;
	
	cpu_set_t cpuset;
	pthread_cond_init( &tasks_completed, NULL );
	pthread_cond_init( &current_task, NULL );

	pthread_mutex_init( &queue_mutex , NULL);

	__lock();
	
	for( i = 0 ; i < max_workers; i ++ ) {
		thr_id[i]	= i;
		pthread_attr_init( &p_attr[i] );
//		pthread_attr_setstacksize( &p_attr[i], 256 * 1024 );
		pthread_attr_setinheritsched( &p_attr[i], PTHREAD_EXPLICIT_SCHED );
		pthread_attr_setschedpolicy( &p_attr[i], SCHED_FIFO );
		pthread_attr_setschedparam( &p_attr[i], &param );

		if( n_cpu > 1 ) {
			int selected_cpu = ((i+1)%n_cpu);
			CPU_ZERO(&cpuset);
			CPU_SET( selected_cpu, &cpuset );
			
			if(pthread_attr_setaffinity_np( &p_attr[i], sizeof(cpuset), &cpuset ) != 0 )
				veejay_msg(0,"Unable to set CPU %d affinity to thread %d", ((i+1)%n_cpu),i);
			else
				veejay_msg(VEEJAY_MSG_DEBUG, "Task thread %d has CPU affinity %d",
					i, selected_cpu ); 
			
		}

		*p_thread_args[i] = i;

		if( pthread_create(  &p_threads[i], (void*) &p_attr[i], task_thread, p_thread_args[i] ) )
		{
			veejay_msg(0, "%s: error starting thread %d/%d", __FUNCTION__,i,max_workers );
			
			memset( &p_threads[i], 0, sizeof(pthread_t) );
			return -1;
		}
	}

	numThreads = max_workers;

	__unlock();

	return numThreads;
}

int	num_threaded_tasks()
{
	return numThreads;
}

void		task_stop(int max_workers)
{
	int i;

	__lock();	
	exitFlag = 1;
	pthread_cond_broadcast( &current_task );
	__unlock();

	for( i = 0; i < max_workers;i++ ) {
		pthread_join( p_threads[i], (void*)&p_tasks[i] );
		pthread_attr_destroy( &p_attr[i] );
	}

	pthread_mutex_destroy( &queue_mutex );
	pthread_cond_destroy( &tasks_completed );
	pthread_cond_destroy( &current_task );	

	task_init();
	
}

void	performer_job( int n )
{
	uint8_t i;
	__lock();
	tasks_todo = n;
	veejay_memset( tasks_done, 0, sizeof(tasks_done));

	for( i = 0; i < n; i ++ ) {
		pjob_t *slot  = job_list[i];
		task_add( i, slot->job, slot->arg );
	}

	__unlock();

	int stop = 0;

	while(!stop) {
		__lock();
		uint8_t done = 0;
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

		__unlock();
	}
}

void	vj_task_set_float( float f ){
	int i;
	int n = task_get_workers();
	for( i = 0; i < n; i ++ )
		vj_task_args[i]->fparam = f;
}

void	vj_task_set_int( int val ){
	int i;
	int n = task_get_workers();

	for( i = 0; i < n; i ++ )
		vj_task_args[i]->iparam = val;
}
void	vj_task_set_param( int val , int idx ){
	int i;
	int n = task_get_workers();

	for( i = 0; i < n; i ++ )
		vj_task_args[i]->iparams[idx] = val;
}

void	vj_task_set_overlap( int val ){
	int i;
	int n = task_get_workers();

	for( i = 0; i < n; i ++ )
		vj_task_args[i]->overlap = val;
}

void	vj_task_set_wid( int w ) {
	int i;
	int n = task_get_workers();

	for( i = 0; i < n; i ++ ) {
		vj_task_args[i]->subwid = w;
		vj_task_args[i]->width = w;
	}
}
void	vj_task_set_hei( int h ) {
	int i;
	int n = task_get_workers();

	for( i = 0; i < n; i ++ ) {
		vj_task_args[i]->height = h;
		vj_task_args[i]->subhei = h;
	}
}

int	vj_task_available()
{
	return ( task_get_workers() > 1 ? 1 : 0);
}

void	vj_task_set_shift( int v, int h )
{
	int i;  
	int n = task_get_workers();

	for( i = 0; i < n; i ++ ) {
		vj_task_args[i]->shifth = h;
		vj_task_args[i]->shiftv = v;
	}
}

void	*vj_task_alloc_internal_buf( unsigned int size )
{
	int i;
	int n = task_get_workers();
#ifdef STRICT_CHECKING
	assert( size > 0 );
#endif
	uint8_t *buffer     =	 (uint8_t*) vj_malloc( size );

	for( i = 0; i < n; i ++ ) {
		vj_task_args[i]->priv = (void*) (buffer + ( size * i ));
	}
	return (void*) buffer;
}

void	vj_task_set_ptr( void *ptr )
{
	int i;
	int n = task_get_workers();

	for( i = 0; i < n; i ++ ) {
		vj_task_args[i]->ptr = ptr;
	}
}

void	vj_task_set_from_args( int len, int uv_len )
{
	int n = task_get_workers();
	int i;
	
	for( i = 0; i < n; i ++ ) {
		vj_task_arg_t *v = vj_task_args[i];
		v->strides[0]	 = len / n;
		v->strides[1]	 = uv_len / n;
		v->strides[2]	 = uv_len / n;
		v->strides[3]    = 0;
	}

}

void	vj_task_set_to_frame( VJFrame *in, int i, int job )
{
	vj_task_arg_t *first = vj_task_args[job];
	in->width = first->width;
	in->height= first->height;
	in->ssm   = first->ssm;
	if( first->ssm ) {
		in->uv_width = first->width;
		in->uv_height= first->uv_height;
		in->uv_len   = (in->width * in->height);
		in->shift_v  = 0;
		in->shift_h  = 0;
	} else {
		in->uv_width = first->uv_width;
		in->uv_height= first->uv_height;
		in->shift_v = first->shiftv;
		in->shift_h = first->shifth;
		in->uv_len  = first->uv_width * first->uv_height;
	}
	in->len     = first->width * first->height;

	switch( i ) {
		case 0:
			in->data[0]=first->input[0];
			in->data[1]=first->input[1];
			in->data[2]=first->input[2];
			break;
		case 1:
			in->data[0]=first->output[0];
			in->data[1]=first->output[1];
			in->data[2]=first->output[2];
			break;
		case 2:
			in->data[0]=first->temp[0];
			in->data[1]=first->temp[1];
			in->data[2]=first->temp[2];
			break;	
	}
}

void	vj_task_set_from_frame( VJFrame *in )
{
	int n = task_get_workers();
	int i;

	for( i = 0; i < n; i ++ ) {
		vj_task_arg_t *v= vj_task_args[i];
		v->ssm		= in->ssm;
		v->width	= in->width;
		v->height	= in->height / n;
		v->subwid	= in->width / n;
		v->subhei	= in->height / n;
		v->strides[0]	= (v->width * v->height);
		v->uv_width	= in->uv_width;
		v->uv_height 	= in->uv_height / n;
		v->strides[1]	= v->uv_width * v->uv_height; //(v->width * v->uv_height) / n;
		v->strides[2]	= v->strides[1];
		v->shiftv	= in->shift_v;
		v->shifth	= in->shift_h;	
			
		if( v->sampling ) {		
			if( v->ssm == 0 ) { //@ super sample to 4:4:4
				v->out_strides[1] = (v->width * v->height);
			} else { //@ subsample 4:2:2
				v->out_strides[1] = (v->uv_width * v->uv_height );
				v->strides[1]     = (v->width * v->height);
			}
			
			v->out_strides[0] =  0;
			v->out_strides[2] = v->out_strides[1];
			v->strides[2] = v->strides[1];
			v->strides[0] = 0;			
		} else {
		        if( v->ssm == 1 ) { //@ FX in 4:4:4
					v->strides[1] = (v->width * v->height );
					v->strides[2] = v->strides[1];
				}
		}
		v->strides[3]   = 0;
	
	}	
}

void	vj_task_set_sampling( int s )
{
	int n = task_get_workers();
	int i;
	
	for( i = 0; i < n; i ++ ) {
		vj_task_arg_t *v = vj_task_args[i];
		v->sampling = s;
	}
}

extern int  pixel_Y_lo_;

int	vj_task_run(uint8_t **buf1, uint8_t **buf2, uint8_t **buf3, int *strides,int n_planes, performer_job_routine func )
{
	int n = task_get_workers();
	if( n <= 1 )
		return 0;

	vj_task_arg_t **f = (vj_task_arg_t**) vj_task_args;
	int i,j;

	for ( i = 0; i < n_planes; i ++ ) {
		if( f[0]->sampling == 1 && i == 0 ){
			f[0]->input[i] = NULL;
			f[0]->output[i] = NULL;
			f[0]->temp[i] = NULL;
		} else {
			f[0]->input[i] = buf1[i];
			f[0]->output[i]= buf2[i];
			if( buf3 != NULL )
				f[0]->temp[i]  = buf3[i];
		}
	}

	f[0]->jobnum = 0;

	if( strides != NULL ) {
		for( j = 0; j < n; j ++ ) {
			for( i = 0; i < n_planes; i ++ ) {
				f[j]->strides[i] = strides[i] / n;
			}
		}
	}

	for( j = 1; j < n; j ++ ) {
		for( i = 0; i < n_planes; i ++ ) {
			if( f[j]->strides[i] == 0 )
				continue;
			if( f[j]->sampling ) {
				f[j]->input[i] = buf1[i] + (f[j]->out_strides[i] * j);
			} else {
				f[j]->input[i]  = buf1[i] + (f[j]->strides[i] * j);
			}
			f[j]->output[i] = buf2[i] + (f[j]->strides[i] * j);
			if( buf3 != NULL )
				f[j]->temp[i]   = buf3[i] + (f[j]->strides[i]* j); 
			f[j]->jobnum = j;
		}	
	}
	
	for( i = 0; i < n; i ++ ) {
		job_list[i]->job = func;
		job_list[i]->arg = f[i];
	}	

	for( j = 0; j < n; j ++ ) {
		if( f[j]->overlap > 0 ) {
			f[j]->overlaprow = (uint8_t*) vj_malloc( sizeof(uint8_t) * f[j]->overlap );
			int offset       =  (f[j]->strides[0] * j) + (f[j]->strides[0] - f[j]->overlap);
			if( (offset+f[j]->overlap) > (f[j]->width * f[j]->height * n) ) {
				veejay_memset( f[j]->overlaprow, pixel_Y_lo_, f[j]->overlap );
			} else {	
				veejay_memcpy(
					f[j]->overlaprow,
					f[j]->input[0] + offset,
					f[j]->overlap );
			}
		}
	}

	performer_job( n );

	for( j = 0; j < n; j ++ ) {
		if( f[j]->overlaprow ) {
			free(f[j]->overlaprow);
			f[j]->overlaprow = NULL;
		}
		f[j]->overlap = 0;
		f[j]->sampling = 0;
	}

	return 1;
}

void	vj_task_free_internal_buf()
{
	int n = task_get_workers();
	int i;
	
	for( i = 0; i < n; i ++ )
		vj_task_args[0]->priv = NULL;
}

