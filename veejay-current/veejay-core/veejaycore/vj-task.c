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
#include <pthread.h>
#include <sched.h>
#include <veejaycore/defs.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libavutil/pixfmt.h>
#include <veejaycore/vj-task.h>
#include <veejaycore/avcommon.h>
#include <stdatomic.h>

//@ job description
static	vj_task_arg_t *vj_task_args[MAX_WORKERS];

//@ job structure
typedef struct {
	performer_job_routine	job;
	void			*arg;
} pjob_t;

typedef struct {
    pjob_t *queue;
    int queue_size;
    int front;
    int rear;
    pthread_mutex_t lock;
    pthread_cond_t task_available;
    pthread_cond_t task_completed;
    pthread_t threads[MAX_WORKERS];
    atomic_int stop_flag;
} thread_pool_t;


static unsigned int n_cpu = 0;
static uint8_t numThreads = 0;
static thread_pool_t *task_pool = NULL;

static uint8_t  task_get_workers()
{
        return numThreads;
}


int	vj_task_get_num_cpus()
{
	if( n_cpu > 0 )
	    return n_cpu;

	long ret = sysconf( _SC_NPROCESSORS_ONLN );
	if( ret <= 0)
		n_cpu = 1;
	else 
		n_cpu = (unsigned int) ret;

	if( n_cpu > MAX_WORKERS ) {
	    n_cpu = MAX_WORKERS;
	}

	return n_cpu;
}

void	vj_task_set_float( float f ){
	uint8_t i;
	uint8_t n = task_get_workers();
	for( i = 0; i < n; i ++ )
		vj_task_args[i]->fparam = f;
}

void	vj_task_set_param( int val , int idx ){
	uint8_t i;
	uint8_t n = task_get_workers();

	for( i = 0; i < n; i ++ )
		vj_task_args[i]->iparams[idx] = val;
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
	in->len     = first->width * first->height;
	in->offset  = first->offset;
	if( first->ssm ) {
		in->uv_width = first->width;
		in->uv_height= first->height;
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

	if( first->format == PIX_FMT_RGBA ) {
		in->stride[0] = in->width * 4;
		in->stride[1] = 0;
		in->stride[2] = 0;
		in->stride[3] = 0;
	}
	else {
		in->stride[0] = first->width;
		in->stride[1] = first->uv_width;
		in->stride[2] = first->uv_width;
		in->stride[3] = (first->strides[3] > 0 ? first->width : 0);
	}


	switch( i ) {
		case 0:
			in->data[0]=first->input[0];
			in->data[1]=first->input[1];
			in->data[2]=first->input[2];
			in->data[3]=first->input[3];
			break;
		case 1:
			in->data[0]=first->output[0];
			in->data[1]=first->output[1];
			in->data[2]=first->output[2];
			in->data[3]=first->output[3];
			break;
		case 2:
			in->data[0]=first->temp[0];
			in->data[1]=first->temp[1];
			in->data[2]=first->temp[2];
			in->data[3]=first->temp[3];
			break;	
	}

	in->format = first->format;
}

void	vj_task_set_from_frame( VJFrame *in )
{
	const uint8_t n = task_get_workers();
	uint8_t i;

	if( in->format == PIX_FMT_RGBA ) 
	{
		for( i = 0; i < n; i ++ ) {
			vj_task_arg_t *v= vj_task_args[i];
			v->ssm			= in->ssm;
			v->width		= in->width;
			v->height		= in->height / n;
			v->strides[0]	= (v->width * v->height * 4);
			v->uv_width		= 0;
			v->uv_height 	= 0;
			v->strides[1]	= 0; 
			v->strides[2]	= 0;
			v->strides[3]   = 0;
			v->shiftv	    = 0;
			v->shifth	    = 0;	
			v->format		  = in->format;
			v->ssm            = 0;
			v->offset		= i * v->strides[0];
		}
	}
	else
	{
		for( i = 0; i < n; i ++ ) {
			vj_task_arg_t *v= vj_task_args[i];
			v->ssm			= in->ssm;
			v->width		= in->width;
			v->height		= in->height / n;
			v->strides[0]	= (v->width * v->height);
			v->uv_width		= in->uv_width;
			v->uv_height 	= in->uv_height / n;
			v->strides[1]	= v->uv_width * v->uv_height; 
			v->strides[2]	= v->strides[1];
			v->strides[3]   = (in->stride[3] == 0 ? 0 : v->strides[0]);
			v->shiftv	    = in->shift_v;
			v->shifth	    = in->shift_h;	
			v->format		  = in->format;
			v->offset       = i * v->strides[0];
			if( v->ssm == 1 ) { 
				v->strides[1] = v->strides[0];
				v->strides[2] = v->strides[1];
			}
		}
	}	
}


static void* task_worker(void *arg) {
    thread_pool_t *pool = (thread_pool_t *)arg;

    while (1) {
        pthread_mutex_lock(&pool->lock);

        while (pool->front == pool->rear && !atomic_load(&pool->stop_flag)) {
            pthread_cond_wait(&pool->task_available, &pool->lock);
        }

        if (atomic_load(&pool->stop_flag)) {
            pthread_mutex_unlock(&pool->lock);
            pthread_exit(NULL);
        }

        pjob_t task = pool->queue[pool->front];
        pool->front = (pool->front + 1) % pool->queue_size;
        pthread_cond_signal(&pool->task_completed);

        pthread_mutex_unlock(&pool->lock);

        task.job(task.arg);

	task.job = NULL;
	task.arg = NULL;
    }

    return NULL;
}

static thread_pool_t* create_thread_pool(int num_threads, int queue_size) {
    thread_pool_t *pool = (thread_pool_t *)malloc(sizeof(thread_pool_t));
    pool->queue = (pjob_t *)malloc(sizeof(pjob_t) * queue_size);
    pool->queue_size = queue_size;
    pool->front = 0;
    pool->rear = 0;
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->task_available, NULL);
    pthread_cond_init(&pool->task_completed, NULL);
    atomic_init(&pool->stop_flag, 0);

    for (int i = 0; i < num_threads; ++i) {
        pthread_create(&(pool->threads[i]), NULL, task_worker, (void *)pool);
    }

    veejay_msg(VEEJAY_MSG_INFO, "Created thread pool with %d workers" , num_threads );

    return pool;
}

static void submit_job(thread_pool_t *pool, performer_job_routine job, void *arg) {
    pthread_mutex_lock(&pool->lock);

    while ((pool->rear + 1) % pool->queue_size == pool->front) {
        pthread_cond_wait(&pool->task_completed, &pool->lock);
    }

    pool->queue[pool->rear].job = job;
    pool->queue[pool->rear].arg = arg;
    pool->rear = (pool->rear + 1) % pool->queue_size;

    pthread_cond_signal(&pool->task_available);
    pthread_mutex_unlock(&pool->lock);
}

static void wait_all_tasks_completed(thread_pool_t *pool) {
    pthread_mutex_lock(&pool->lock);
    while (pool->front != pool->rear) {
        pthread_cond_wait(&pool->task_completed, &pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);
}

static void destroy_thread_pool(thread_pool_t *pool) {
    pthread_mutex_lock(&pool->lock);
    atomic_store(&pool->stop_flag, 1);
    pthread_cond_broadcast(&pool->task_available);
    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < 4; ++i) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->task_available);
    pthread_cond_destroy(&pool->task_completed);

    free(pool->queue);
    free(pool);
}


int	vj_task_run(uint8_t **buf1, uint8_t **buf2, uint8_t **buf3, int *strides,int n_planes, performer_job_routine func )
{
	const uint8_t n = task_get_workers();
	if( n <= 1 ) {
        	return 0;
    	}

	vj_task_arg_t **f = (vj_task_arg_t**) vj_task_args;
	uint8_t i,j;

	for ( i = 0; i < n_planes; i ++ ) { 
		f[0]->input[i] = buf1[i]; //frame
		f[0]->output[i]= buf2[i]; //frame2
	}

	if( buf3 != NULL ) {
		for( i = 0; i < n_planes; i ++ ) {
			f[0]->temp[i]  = buf3[i];
		}
	}

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
				f[j]->temp[i] = buf3[i] + (f[j]->strides[i]* j); 
		}
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "New job!" );
	for( i = 0; i < n; i ++ ) {
		f[i]->jobnum = i;
		veejay_msg(VEEJAY_MSG_DEBUG, "Queue task %d [%p, %p] [ %d %d %d ]",
				f[i]->input[0],
				f[i]->output[0],
				f[i]->strides[0],
				f[i]->strides[1],
				f[i]->strides[2] );

		submit_job( task_pool, func, f[i] );
	}	

	wait_all_tasks_completed(task_pool);
	for( i = 0; i < n; i ++ ) {
		veejay_memset( f[i], 0, sizeof(vj_task_arg_t));
	}

	return 1;
}



void task_destroy()
{
	destroy_thread_pool( task_pool );
}

void task_init() 
{
	vj_task_get_num_cpus();

	numThreads = n_cpu/2;

	char *task_cfg = getenv( "VEEJAY_MULTITHREAD_TASKS" );
	if( task_cfg != NULL ) {
		numThreads = atoi( task_cfg );	
	}

	if( numThreads < 0 || numThreads > MAX_WORKERS ) {
		numThreads = n_cpu;
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid value for VEEJAY_MULTITHREAD_TASKS, using default (%d)",numThreads);
	}

	int i;
	for( i = 0; i < MAX_WORKERS ; i ++ ) {
	    vj_task_args[i] = vj_calloc( sizeof(vj_task_arg_t) );
	}

	task_pool = create_thread_pool( n_cpu, numThreads );
}
