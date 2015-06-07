/* 
 * Linux VeeJay
 *
 * Copyright(C)2012-2015 Niels Elburg <nwelburg@gmail.com>
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
#include <sched.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-misc.h>
#include <veejay/vj-lib.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libvje/internal.h>
#include <veejay/vj-task.h>
#include <libvje/vje.h>
 
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
static struct task running_tasks[MAX_WORKERS];
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tasks_completed;
static pthread_cond_t current_task;
struct task *tasks_ = NULL;
struct task *tail_task_= NULL;
static pthread_t p_threads[MAX_WORKERS];
static uint8_t *p_thread_args[MAX_WORKERS];
static pthread_attr_t p_attr[MAX_WORKERS];
static	pjob_t *job_list[MAX_WORKERS];
static uint8_t p_tasks[MAX_WORKERS];
static pjob_t *job_list[MAX_WORKERS];
static int n_cpu = 1;
static uint8_t numThreads = 0;
static uint8_t total_tasks = 0;
static uint8_t tasks_done[MAX_WORKERS];
static uint8_t tasks_todo = 0;
static int exitFlag = 0;
static struct task running_tasks[MAX_WORKERS];
static struct sched_param param;
static int euid = 0;

#define __lock() pthread_mutex_lock(&queue_mutex)
#define __unlock() pthread_mutex_unlock(&queue_mutex)

static void		task_allocate()
{
	unsigned int i;
	for( i = 0; i < MAX_WORKERS; i ++ ) {
		job_list[i] = vj_malloc(sizeof(pjob_t));
		vj_task_args[i] = vj_calloc(sizeof(vj_task_arg_t));
		p_thread_args[i] = vj_calloc( sizeof(uint8_t) );
	}

	n_cpu = sysconf( _SC_NPROCESSORS_ONLN );
	if( n_cpu <= 0 )
	{
		n_cpu = 1;
	}
}

void		task_destroy()
{
	unsigned int i;
	for( i = 0; i < MAX_WORKERS; i ++ ) {
		free(job_list[i]);
		free(vj_task_args[i]);
		free(p_thread_args[i]);
	}
}

static void		task_reset()
{
	unsigned int i;

	veejay_memset( &p_threads,0,sizeof(p_threads));
	veejay_memset( &p_tasks,0,sizeof(p_tasks));
	for( i = 0; i < MAX_WORKERS; i ++ ) {
		veejay_memset( job_list[i],0, sizeof(pjob_t));
		veejay_memset( vj_task_args[i],0, sizeof(vj_task_arg_t));
		veejay_memset( p_thread_args[i],0, sizeof(uint8_t));
		veejay_memset( &(running_tasks[i]), 0, sizeof(struct task));
	}

	numThreads = 0;
}

static void	task_add(uint8_t task_no, performer_job_routine fp , void *data)
{
	struct task *qp_task = &(running_tasks[task_no]);

	qp_task->task_id 	=	task_no;
	qp_task->handler	=	fp;
	qp_task->data	= 	data;
	qp_task->next	= 	NULL;

	if( total_tasks == 0 ) {
		tasks_		=	qp_task;
		tail_task_	=	tasks_;
	}
	else {
		tail_task_->next=	qp_task;
		tail_task_	=	qp_task;
	}

	total_tasks ++;
}

struct	task	*task_get()
{
	struct task *t = NULL;
	if( total_tasks > 0  ) {
		t 	=	tasks_;
		tasks_	=	tasks_->next;

		if( tasks_ == NULL ) {
			tail_task_ = NULL;
		}

		total_tasks --;
	}
	return t;
}

void		task_run( struct task *task, void *data, uint8_t id)
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
	const uint8_t id = (const uint8_t) *((uint8_t *) data);
	for( ;; ) 
	{
		struct task *t = NULL;

		__lock();
		while( total_tasks == 0 ) {
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
		}
	}
}

static __inline__ uint8_t	task_get_workers()
{
	return numThreads;
}

void		task_init()
{
	task_allocate();
 
	int max_p = sched_get_priority_max( SCHED_FIFO );
	int min_p = sched_get_priority_min( SCHED_FIFO );

	max_p = (int) ( ((float) max_p) * 0.95f );
	if( max_p < min_p )
    		max_p = min_p;

	veejay_memset( &param, 0, sizeof(param));
	euid = geteuid();
	if( euid == 0 ) {
		param.sched_priority =  max_p;
	} 
}

int		task_num_cpus()
{
	return n_cpu;
}

int		task_start(unsigned int max_workers)
{
	uint8_t i;
	if( max_workers >= MAX_WORKERS ) {
		veejay_msg(0, "Maximum number of threads is %d", MAX_WORKERS );
		return 0;
	}
	exitFlag = 0;

	cpu_set_t cpuset;
	pthread_cond_init( &tasks_completed, NULL );
	pthread_cond_init( &current_task, NULL );
	
	for( i = 0 ; i < max_workers; i ++ ) {
		pthread_attr_init( &p_attr[i] );
		if( euid == 0 ) {
			pthread_attr_setinheritsched( &p_attr[i], PTHREAD_EXPLICIT_SCHED );
			pthread_attr_setschedpolicy( &p_attr[i], SCHED_FIFO );
		}
		pthread_attr_setschedparam( &p_attr[i], &param );

		if( n_cpu > 1 ) {
			unsigned int selected_cpu = ((i+1)%n_cpu);
			CPU_ZERO(&cpuset);
			CPU_SET( selected_cpu, &cpuset );
			
			if(pthread_attr_setaffinity_np( &p_attr[i], sizeof(cpuset), &cpuset ) != 0 )
				veejay_msg(0,"Unable to set CPU %d affinity to thread %d", ((i+1)%n_cpu),i);
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

	return numThreads;
}

uint8_t	num_threaded_tasks()
{
	return numThreads;
}

void		task_stop(unsigned int max_workers)
{
	unsigned int i;

	__lock();	
	exitFlag = 1;
	pthread_cond_broadcast( &current_task );
	__unlock();

	for( i = 0; i < max_workers;i++ ) {
		pthread_join( p_threads[i], (void*)&p_tasks[i] );
		pthread_attr_destroy( &p_attr[i] );
	}

	pthread_cond_destroy( &tasks_completed );
	pthread_cond_destroy( &current_task );	

	task_reset();
	
}

void	performer_job( uint8_t n )
{
	uint8_t i;

	__lock();
	tasks_todo = n;
	for( i = 0; i < n; i ++ ) {
		pjob_t *slot  = job_list[i];
		tasks_done[i] = 0;
		task_add( i, slot->job, slot->arg );
	}
	pthread_cond_signal( &current_task );
	__unlock();

	uint8_t stop = 0;
	while(!stop) {
		uint8_t done = 0;
		
		__lock();

		for( i = 0 ; i < tasks_todo; i ++ ) {
			done += tasks_done[i];
		}

		if( done < tasks_todo ) {
			done = 0;
			pthread_cond_wait( &tasks_completed, &queue_mutex );
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
	uint8_t i;
	uint8_t n = task_get_workers();
	for( i = 0; i < n; i ++ )
		vj_task_args[i]->fparam = f;
}

void	vj_task_set_int( int val ){
	uint8_t i;
	uint8_t n = task_get_workers();

	for( i = 0; i < n; i ++ )
		vj_task_args[i]->iparam = val;
}
void	vj_task_set_param( int val , int idx ){
	uint8_t i;
	uint8_t n = task_get_workers();

	for( i = 0; i < n; i ++ )
		vj_task_args[i]->iparams[idx] = val;
}

uint8_t	vj_task_available()
{
	return ( task_get_workers() > 1 ? 1 : 0);
}

void	vj_task_set_ptr( void *ptr )
{
	uint8_t i;
	uint8_t n = task_get_workers();

	for( i = 0; i < n; i ++ ) {
		vj_task_args[i]->ptr = ptr;
	}
}

void	vj_task_set_from_args( int len, int uv_len )
{
	uint8_t n = task_get_workers();
	uint8_t i;
	
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
	uint8_t n = task_get_workers();
	uint8_t i;

	for( i = 0; i < n; i ++ ) {
		vj_task_arg_t *v= vj_task_args[i];
		v->ssm		= in->ssm;
		v->width	= in->width;
		v->height	= in->height / n;
		v->strides[0]	= (v->width * v->height);
		v->uv_width	= in->uv_width;
		v->uv_height 	= in->uv_height / n;
		v->strides[1]	= v->uv_width * v->uv_height; 
		v->strides[2]	= v->strides[1];
		v->shiftv	= in->shift_v;
		v->shifth	= in->shift_h;	
			
		if( v->ssm == 1 ) { //@ FX in 4:4:4
			v->strides[1] = (v->width * v->height );
			v->strides[2] = v->strides[1];
		}
		
		v->strides[3]   = 0;
	}	
}

int	vj_task_run(uint8_t **buf1, uint8_t **buf2, uint8_t **buf3, int *strides,int n_planes, performer_job_routine func )
{
	uint8_t n = task_get_workers();
	if( n <= 1 )
		return 0;

	vj_task_arg_t **f = (vj_task_arg_t**) vj_task_args;
	uint8_t i,j;

	for ( i = 0; i < n_planes; i ++ ) {
		f[0]->input[i] = buf1[i];
		f[0]->output[i]= buf2[i];
	}

	if( buf3 != NULL ) {
		for( i = 0; i < n_planes; i ++ ) {
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
			f[j]->input[i]  = buf1[i] + (f[j]->strides[i] * j);
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

	performer_job( n );

	for( i = 0; i < n; i ++ ) {
		veejay_memset( f[i], 0, sizeof(vj_task_arg_t));
	}

	return 1;
}

void	vj_task_free_internal_buf()
{
	uint8_t n = task_get_workers();
	uint8_t i;
	
	for( i = 0; i < n; i ++ )
		vj_task_args[0]->priv = NULL;
}

