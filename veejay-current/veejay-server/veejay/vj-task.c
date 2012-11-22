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
#include <veejay/vj-misc.h>
#include <veejay/vj-lib.h>
#include <libvjmem/vjmem.h>
#include <libvje/internal.h>
#include <veejay/vj-task.h>

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

static	vj_task_arg_t *vj_task_args[MAX_WORKERS];

struct task
{
        int     task_id;
 	void	*data;
	void	*(*handler)(void *arg);
	struct  task    *next;
};

typedef struct {
	performer_job_routine	job;
	void			*arg;
} pjob_t;

static int	total_tasks_	=	0;
static int tasks_done[MAX_WORKERS];
static int tasks_todo = 0;
static int exitFlag = 0;
static int taskLock = 0;
static pthread_mutex_t	queue_mutex;
static pthread_cond_t 	tasks_completed;
static pthread_cond_t	current_task;
static	int	numThreads	= 0;
struct	task	*tasks_		= NULL;
struct	task	*tail_task_	= NULL;
 
static pthread_t p_threads[MAX_WORKERS];
static pthread_attr_t p_attr[MAX_WORKERS];
static int	 p_tasks[MAX_WORKERS];
static int	 thr_id[MAX_WORKERS];
static	pjob_t *job_list[MAX_WORKERS];

#define    RUP8(num)(((num)+8)&~8)

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
	memset( &vj_task_args,0,sizeof(vj_task_arg_t*) * MAX_WORKERS );
	for( i = 0; i < MAX_WORKERS; i ++ ) {
		job_list[i] = vj_malloc(sizeof(pjob_t));
		vj_task_args[i] = vj_malloc(sizeof(vj_task_arg_t));
		memset( job_list[i], 0, sizeof(pjob_t));
		memset( vj_task_args[i], 0, sizeof(vj_task_arg_t));
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

int	num_threaded_tasks()
{
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

void	vj_task_set_float( float f ){
	int i;
	for( i = 0; i < MAX_WORKERS; i ++ )
		vj_task_args[i]->fparam = f;
}

void	vj_task_set_int( int val ){
	int i;
	for( i = 0; i < MAX_WORKERS; i ++ )
		vj_task_args[i]->iparam = val;
}

void	vj_task_set_wid( int w ) {
	int i;
	for( i = 0; i < MAX_WORKERS; i ++ )
		vj_task_args[i]->width = w;
}
void	vj_task_set_hei( int h ) {
	int i;
	for( i = 0; i < MAX_WORKERS; i ++ )
		vj_task_args[i]->height = h;
}

int	vj_task_available()
{
	return ( task_get_workers() > 1 ? 1 : 0);
}

void	vj_task_set_shift( int v, int h )
{
	int i;  
	for( i = 0; i < MAX_WORKERS; i ++ ) {
		vj_task_args[i]->shifth = h;
		vj_task_args[i]->shiftv = v;
	}
}

void	vj_task_alloc_internal_buf( int w )
{
	int i;
	for( i = 0; i < MAX_WORKERS; i ++ ) {
		vj_task_args[i]->priv = (void*) vj_malloc( RUP8( w ) );
	}
}


int	vj_task_run(uint8_t **buf1, uint8_t **buf2, uint8_t **buf3, int *strides,int n_planes, performer_job_routine func )
{
	int n = task_get_workers();
	if( n <= 1 )
		return 0;

	vj_task_arg_t **f = (vj_task_arg_t**) vj_task_args;
	int i,j;
	
	if( strides != NULL ) {
		for( j = 0; j < n; j ++ ) {
			for( i = 0; i < n_planes; i ++ ) {
				f[j]->strides[i] = strides[i] / n;
			}
		}

		for ( i = 0; i < n_planes; i ++ ) {
			if( f[0]->strides[i] == 0 )
				continue;

			f[0]->input[i] = buf1[i];
			f[0]->output[i]= buf2[i];
			
			if( buf3 != NULL )
				f[0]->temp[i]  = buf3[i];
		}

		for( j = 1; j < n; j ++ ) {
			for( i = 0; i < n_planes; i ++ ) {
				if( strides[i] == 0 )
					continue;
				f[j]->input[i]  = buf1[i] + (f[0]->strides[i] * j);
				f[j]->output[i] = buf2[i] + (f[0]->strides[i] * j);
				if( buf3 != NULL )
					f[j]->temp[i]   = buf3[i] + (f[0]->strides[i]* j); 
			}
		}
	} else if( f[0]->width > 0 && f[0]->height > 0 ) {
		int w = f[0]->width;
		int h = f[0]->height;
	
		for( j = 0; j < n; j ++ ) {
			f[j]->height = h / n;
		}

		int uv_width = w >> f[0]->shiftv;
		int uv_height= h >> f[0]->shifth;

		int wid  = w;
		int uwid = uv_width;

		int hei = h / n;
		int uhei = hei >> f[0]->shifth;

		for( j = 0; j < n; j ++ ) {
			f[j]->input[0] = buf1[0] + ( j * wid * hei );
		        f[j]->input[1] = buf1[1] + ( j * uwid * uhei );	
			f[j]->input[2] = buf1[2] + ( j * uwid * uhei );
			f[j]->input[3] = NULL;
			f[j]->output[0]= buf2[0] + ( j * wid * uhei );
			f[j]->output[1]= buf2[1] + ( j * uwid * uhei );
			f[j]->output[2]= buf2[2] + ( j * uwid * uhei );
			f[j]->output[3] = NULL;
			if( buf3 != NULL ) {
				f[j]->temp[0]  = buf3[0] + ( j * wid * hei );
				f[j]->temp[1]  = buf3[1] + ( j * uwid * uhei );
				f[j]->temp[2]  = buf3[2] + ( j * uwid * uhei );
				f[j]->temp[3] = NULL;
			}
			for ( i = 0; i < 4; i ++ )
				f[j]->strides[i] = 0;
		}

	}
	else {
		veejay_msg(0, "%s: Invalid arguments.",__FUNCTION__);
#ifdef STRICT_CHECKING
		assert( 0 );
#endif
		return 0;
	}


	for( i = 0; i < n; i ++ ) {
		job_list[i]->job = func;
		job_list[i]->arg = f[i];
	}	

	performer_job( n );

	for( i = 0; i < n; i ++ ) {
		if( f[i]->priv )
			free( f[i]->priv );
	}

	return 1;
}


