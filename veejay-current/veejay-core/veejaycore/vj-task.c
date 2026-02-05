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
static  vj_task_arg_t *vj_task_args[MAX_WORKERS];
static pthread_key_t thread_buf_key;
static int thread_buf_size = 0;

//@ job structure
typedef struct {
    performer_job_routine   job;
    void            *arg;
} pjob_t;

typedef struct {
    pjob_t *queue;
    int queue_size;
    pthread_mutex_t lock;
    pthread_cond_t start_signal;
    pthread_cond_t task_completed;
    pthread_t threads[MAX_WORKERS];
    atomic_int stop_flag;
    int num_submitted_tasks;
    int num_completed_tasks;
    uint8_t ***thread_local_bufs;
} thread_pool_t;

typedef struct {
    thread_pool_t *pool;
    int job_num;
} task_thread_args_t;

static task_thread_args_t *thread_args[MAX_WORKERS];

static unsigned int n_cpu = 0;
static uint8_t numThreads = 0;
static thread_pool_t *task_pool = NULL;

uint8_t  vj_task_get_workers()
{
        return numThreads;
}

void    vj_task_lock() {
    pthread_mutex_lock(&task_pool->lock);
}
void    vj_task_unlock() {
    pthread_mutex_unlock(&task_pool->lock);
}

int vj_task_get_num_cpus()
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

void    vj_task_set_float( float f ){
    uint8_t i;
    uint8_t n = vj_task_get_workers();
    for( i = 0; i < n; i ++ )
        vj_task_args[i]->fparam = f;
}

void    vj_task_set_param( int val , int idx ){
    uint8_t i;
    uint8_t n = vj_task_get_workers();

    for( i = 0; i < n; i ++ )
        vj_task_args[i]->iparams[idx] = val;
}

void    vj_task_set_ptr( void *ptr )
{
    uint8_t i;
    uint8_t n = vj_task_get_workers();

    for( i = 0; i < n; i ++ ) {
        vj_task_args[i]->ptr = ptr;
    }
}

void    vj_task_set_from_args( int len, int uv_len )
{
    uint8_t n = vj_task_get_workers();
    uint8_t i;
    
    for( i = 0; i < n; i ++ ) {
        vj_task_arg_t *v = vj_task_args[i];
        v->strides[0]    = len / n;
        v->strides[1]    = uv_len / n;
        v->strides[2]    = uv_len / n;
        v->strides[3]    = 0; 
    }
}

void    vj_task_set_to_frame( VJFrame *in, int i, int job )
{
    vj_task_arg_t *first = vj_task_args[job];

    in->local = first->local;
    in->jobnum = job;
    in->totaljobs = numThreads;
    in->out_width = first->out_width;
    in->out_height = first->out_height;
    in->timecode = first->timecode;
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

void    vj_task_set_from_frame( VJFrame *in )
{
    const uint8_t n = vj_task_get_workers();
    uint8_t i;

    if( in->format == PIX_FMT_RGBA ) 
    {
        for( i = 0; i < n; i ++ ) {
            vj_task_arg_t *v= vj_task_args[i];
            v->ssm          = in->ssm;
            v->width        = in->width;
            v->height       = in->height / n;
            v->strides[0]   = (v->width * v->height * 4);
            v->uv_width     = 0;
            v->uv_height    = 0;
            v->strides[1]   = 0; 
            v->strides[2]   = 0;
            v->strides[3]   = 0;
            v->shiftv       = 0;
            v->shifth       = 0;    
            v->format         = in->format;
            v->ssm            = 0;
            v->offset       = i * v->strides[0];
            v->out_width    = in->width;
            v->out_height   = in->height;
            v->timecode     = in->timecode;
        }
    }
    else
    {
        for( i = 0; i < n; i ++ ) {
            vj_task_arg_t *v= vj_task_args[i];
            v->ssm          = in->ssm;
            v->width        = in->width;
            v->height       = in->height / n;
            v->strides[0]   = (v->width * v->height);
            v->uv_width     = in->uv_width;
            v->uv_height    = in->uv_height / n;
            v->strides[1]   = v->uv_width * v->uv_height; 
            v->strides[2]   = v->strides[1];
            v->strides[3]   = (in->stride[3] == 0 ? 0 : v->strides[0]);
            v->shiftv       = in->shift_v;
            v->shifth       = in->shift_h;  
            v->format         = in->format;
            v->offset       = i * v->strides[0];
            v->out_width    = in->width;
            v->out_height   = in->height;
            v->timecode     = in->timecode;
            if( v->ssm == 1 ) { 
                v->strides[1] = v->strides[0];
                v->strides[2] = v->strides[1];
            }
        }
    }   
}

static void init_thread_local_bufs(size_t plane_size) {

    uint8_t **tlbuf = (uint8_t**) vj_malloc( sizeof(uint8_t*) * 4 );
    tlbuf[0] = (uint8_t*) vj_malloc( plane_size * 4 );
    tlbuf[1] = tlbuf[0] + plane_size;
    tlbuf[2] = tlbuf[1] + plane_size;
    tlbuf[3] = tlbuf[2] + plane_size;

    veejay_memset( tlbuf[0], 255, plane_size );
    veejay_memset( tlbuf[1], 128, plane_size );
    veejay_memset( tlbuf[2], 128, plane_size );

    pthread_setspecific( thread_buf_key, tlbuf );

}


static void* task_worker(void *arg) {
    task_thread_args_t *ptr = (task_thread_args_t*) arg;
    thread_pool_t *pool = (thread_pool_t *)ptr->pool;
    int job_num = ptr->job_num;

    init_thread_local_bufs( thread_buf_size );

    uint8_t **tlbuf = (uint8_t**) pthread_getspecific( thread_buf_key );

    pthread_mutex_lock( &(pool->lock) );
    pool->thread_local_bufs[ job_num ] = tlbuf;
    pthread_mutex_unlock( &(pool->lock) );


    while (1) {
        pthread_mutex_lock(&pool->lock);
        while( pool->queue[ job_num ].job == NULL) {
            pthread_cond_wait(&pool->start_signal, &pool->lock);
            if (atomic_load(&pool->stop_flag)) {
                pthread_mutex_unlock(&pool->lock);
                pthread_exit(NULL);
            }   
        }
        pthread_mutex_unlock(&pool->lock);

        if (atomic_load(&pool->stop_flag)) {
            pthread_exit(NULL);
        }

        pjob_t task = pool->queue[ job_num ];
        task.job(task.arg);

        
        pthread_mutex_lock(&pool->lock);
        
        pool->num_completed_tasks ++;
        if( pool->num_completed_tasks == pool->num_submitted_tasks ) {
            pthread_cond_signal( &pool->task_completed );
            pool->num_completed_tasks = 0;
            pool->num_submitted_tasks = 0;
        }
        pool->queue[ job_num ].job = NULL;
        pool->queue[ job_num ].arg = NULL;
        pthread_mutex_unlock(&pool->lock);
    }

    return NULL;
}


static void free_thread_local_bufs(void *buf) {

    uint8_t **tlbuf = (uint8_t**) buf;
    free( tlbuf[0] );

    free( tlbuf );
}

static thread_pool_t* create_thread_pool(int num_threads) {
    thread_pool_t *pool = (thread_pool_t *)vj_calloc(sizeof(thread_pool_t));
    pool->queue = (pjob_t *)vj_calloc(sizeof(pjob_t) * num_threads);
    pool->queue_size = num_threads;
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->task_completed, NULL);
    pthread_cond_init(&pool->start_signal, NULL);

    atomic_init(&pool->stop_flag, 0);

    pthread_key_create( &thread_buf_key, free_thread_local_bufs );

    pool->thread_local_bufs = (uint8_t***) vj_malloc( sizeof(uint8_t**) * num_threads );
    
    for (int i = 0; i < num_threads; i++) {
        task_thread_args_t *args = thread_args[i];
        args->pool = pool;
        args->job_num = i;
        pthread_create(&(pool->threads[i]), NULL, task_worker, args);
    }

    return pool;
}

static void submit_job(thread_pool_t *pool, performer_job_routine job, void *arg) {

    int idx = pool->num_submitted_tasks;

    pool->queue[idx].job = job;
    pool->queue[idx].arg = arg;
    pool->num_submitted_tasks ++;

}

static void start_all_threads(thread_pool_t *pool) {
    pthread_mutex_lock(&pool->lock);
    pthread_cond_broadcast(&pool->start_signal);
    pthread_mutex_unlock(&pool->lock);  
}

static void wait_all_tasks_completed(thread_pool_t *pool) {
    pthread_mutex_lock(&pool->lock);
    while (pool->num_completed_tasks != pool->num_submitted_tasks) {
        pthread_cond_wait(&pool->task_completed, &pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);
}

int vj_task_run(uint8_t **buf1, uint8_t **buf2, uint8_t **buf3, int *strides,int n_planes, performer_job_routine func, int use_thread_local )
{
    const uint8_t n = vj_task_get_workers();
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

    vj_task_lock();
    
    for( i = 0; i < n; i ++ ) {
        f[i]->jobnum = i;
        f[i]->local = task_pool->thread_local_bufs[i];
    }  

    for( i = 0; i < n; i ++ ) {
         submit_job( task_pool, func, f[i] );
    }
    vj_task_unlock();

    start_all_threads(task_pool);   
    
    wait_all_tasks_completed(task_pool);

    if( use_thread_local ) {
        for( i = 0; i < n; i ++ ) {
            for( j = 0; j < n_planes; j ++ ) {
                veejay_memcpy( f[i]->input[j], task_pool->thread_local_bufs[i][j], f[i]->strides[j] );
            }
        }
    }

    return 1;
}

void task_destroy()
{
    if(!task_pool)
        return;

    pthread_mutex_lock(&task_pool->lock);
    atomic_store(&task_pool->stop_flag, 1);
    pthread_cond_broadcast(&task_pool->start_signal);
    pthread_mutex_unlock(&task_pool->lock);

    for (int i = 0; i < numThreads; i++) {
        pthread_join(task_pool->threads[i], NULL);
    }

    free(task_pool->queue);
    free(task_pool->thread_local_bufs);

    pthread_mutex_destroy(&task_pool->lock);
    pthread_cond_destroy(&task_pool->task_completed);
    pthread_cond_destroy(&task_pool->start_signal);

    pthread_key_delete(thread_buf_key);

    free(task_pool);
    task_pool = NULL;
}



void task_init(int w, int h) 
{
    thread_buf_size = w * h;

    vj_task_get_num_cpus();

    numThreads = n_cpu/2;

    char *task_cfg = getenv( "VEEJAY_MULTITHREAD_TASKS" );
    if( task_cfg != NULL ) {
        numThreads = atoi( task_cfg );  
    }

    if( numThreads < 0 || numThreads > MAX_WORKERS ) {
        numThreads = n_cpu/2;
    }

    int i;
    for( i = 0; i < MAX_WORKERS ; i ++ ) {
        vj_task_args[i] = vj_calloc( sizeof(vj_task_arg_t) );
        thread_args[i] = vj_calloc(sizeof(task_thread_args_t));
    }

    task_pool = create_thread_pool( numThreads );
}
