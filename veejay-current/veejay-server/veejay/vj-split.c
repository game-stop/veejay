/* veejay - Linux VeeJay
 * 	     (C) 2015 Niels Elburg <nwelburg@gmail.com> 
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
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <veejay/vj-split.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vj-server.h>
#include <veejaycore/yuvconv.h>
#include <veejay/vj-share.h>
#include <veejaycore/libvevo.h>
#include <libplugger/defs.h>
#include <libplugger/ldefs.h>
#include <libplugger/specs/livido.h>
#include <string.h>

#define LOCALHOST "127.0.0.1" 
#define SHM_ADDR_OFFSET 4096

/**
 * view port of screen
 */
typedef struct
{
	int width;   /* src dimensions */
	int height;

	int left;	 /* crop */
	int right;
	int top;
	int bottom;

	void *shm;   /* box client type */
	void *net;
	
	int edge_v;  /* edge blending vertical distance in pixels */
	int edge_h;  /* edge blending horizontal distance in pixels */

	uint8_t *data;
} v_screen_t;

typedef struct
{
	VJFrame **frames; /* destination frame */
	v_screen_t **screens;  /* screens */
	int n_screens;    /* nr of screens */
	int current_id;   /* auto identifier */
	int rows;
	int columns;
	int off_x;
	int off_y;
} vj_split_t;

typedef struct
{
	int					resource_id;
	pthread_rwlock_t	rwlock;
	int					header[8];
} vj_shared_data;

typedef struct
{
	int shm_id;
	char *sms;
	key_t key;
	pthread_rwlock_t rwlock;
	vj_shared_data *data;
} vj_split_shm_t;

typedef struct
{
	int started;
	char *hostname;
	int port;
} vj_split_net_t;

static char *server_ip = NULL;
static int server_port = 0;

void	vj_split_set_master(int port)
{
	server_ip = vj_server_my_ip();
	server_port = port;
}


static void	*vj_split_net_new( char *hostname, int port, int w, int h)
{
	vj_split_net_t *net = (vj_split_net_t*) vj_calloc(sizeof(vj_split_net_t));
	if(!net)
		return NULL;
	net->hostname = strdup(hostname);
	net->port = port;
	return (void*) net;
}

static void *vj_split_shm_new(key_t key, int w, int h)
{
	vj_split_shm_t *shm = NULL;
	if( key < 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Already in use");
		return NULL;
	}

	long shm_size = (w*h*4);
	int id = shmget( key, shm_size, IPC_CREAT | 0666 );
	if( id == -1 ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Failed to access shared resource %d", key );
		return NULL;
	}

	char *sms = shmat( id, NULL, 0 );
	if( sms == NULL || sms == (char*) (-1)) {
		veejay_msg(VEEJAY_MSG_ERROR,"Failed to attach to shared resource %d",key);
		return NULL;
	}

	shm = (vj_split_shm_t*) vj_malloc(sizeof(vj_split_shm_t));
	shm->shm_id = id;
	shm->sms = sms;
	shm->key = key;

	veejay_memset( shm->sms, 0, SHM_ADDR_OFFSET + (w * h)  );
	veejay_memset( shm->sms + SHM_ADDR_OFFSET + (w*h),128, w*h*2);

	shm->data = (vj_shared_data*) &(shm->sms[0]);

	shm->data->header[0] = w;
	shm->data->header[1] = h;
	shm->data->header[2] = w;
	shm->data->header[3] = w;
	shm->data->header[4] = w;
	shm->data->header[5] = LIVIDO_PALETTE_YUV444P;

	pthread_rwlockattr_t rw_lock_attr;

	int ret = pthread_rwlockattr_init( &rw_lock_attr);
	if( ret == -1 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to acquire rwlock on shared resource %d", key );
		shmctl( shm->shm_id, IPC_RMID, NULL );
		free(shm);
		return NULL;
	}

	ret = pthread_rwlockattr_setpshared( &rw_lock_attr, PTHREAD_PROCESS_SHARED );
	if( ret == -1 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to set PTHREAD_PROCESS_SHARED");
		shmctl( shm->shm_id, IPC_RMID, NULL );
		free(shm);
		return NULL;
	}

	ret = pthread_rwlock_init( &(shm->data->rwlock), &rw_lock_attr);
	if( ret == -1) {
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to initialize rw-lock" );
		shmctl( shm->shm_id, IPC_RMID, NULL );
		free(shm);
		return NULL;
	}

	return (void*) shm;
}

static void vj_split_shm_destroy(vj_split_shm_t *shm)
{
	vj_shared_data *data = (vj_shared_data*) shm->sms;

	int res = pthread_rwlock_destroy( &(data->rwlock) );
	if(res == -1) {
		veejay_msg(VEEJAY_MSG_ERROR,"Failed to destroy rw lock");
	}

	res = shmctl( shm->shm_id, IPC_RMID, NULL );
	if( res == -1 ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Failed to remove shared memory segment %x:%d", shm->key,shm->shm_id);
	}

	free(shm);
}

void	*vj_split_new_from_file(char *filename, int out_w, int out_h, int vfmt)
{
	FILE *f;

	f = fopen( filename,"r" );
	if( f == NULL ) {
		veejay_msg(VEEJAY_MSG_WARNING, "No split screen configured in %s", filename);
		return NULL;
	}
	
	char hostname[1024];
	int row=0,col=0,port=0;
	int max_col=0,max_row=0;

	void *split = NULL;

	veejay_msg(VEEJAY_MSG_INFO,"Splitted screens configured in %s", filename );

	while( !feof(f) ) {
		
		if( fscanf(f,"screen=%dx%d\n", &max_row,&max_col ) ) {
			if( (max_row * max_col) > 0 ) {
				split = vj_split_init( max_row, max_col );
			}
			else {
				veejay_msg(VEEJAY_MSG_ERROR,"Invalid row/columns in configuration file" );
				fclose(f);
				return NULL;
			}
		}

		if (fscanf(f, "row=%d col=%d port=%d hostname=%1023s\n",&row, &col, &port, hostname) == 4) {

            if (!split) {
                veejay_msg(VEEJAY_MSG_ERROR, "Screen not initialized");
                fclose(f);
                return NULL;
            }

            vj_split_add_screen(split, hostname, port, row, col, out_w, out_h, vfmt);
		}
	}

	fclose(f);

	return split;
}

/**
 * initialize splitted screens
 */
void *vj_split_init( int r, int c )
{
	vj_split_t *x = (vj_split_t*) vj_calloc( sizeof(vj_split_t) );
	x->frames = (VJFrame**) vj_calloc( sizeof(VJFrame*) * r * c );
	x->screens = (v_screen_t**) vj_calloc( sizeof(v_screen_t*) * r * c );
	x->n_screens = r * c;
	x->current_id = 0;
	x->rows = r;
	x->columns = c;
	return (void*) x;
}

static void vj_split_free_screen( vj_split_t *x, int screen_id )
{
	if( x->screens[ screen_id ] ) {

		if( x->screens[ screen_id ]->shm ) {
			vj_split_shm_destroy( x->screens[ screen_id ]->shm );
			x->screens[ screen_id ]->shm = NULL;
		}

		if( x->screens[ screen_id ]->net ) {
			vj_split_net_t *net = (vj_split_net_t*) x->screens[ screen_id ]->net;
			free(net->hostname);
			free(net);
			net = NULL;
		}

		if(x->screens[screen_id]->data) {
			free(x->screens[screen_id]->data);
			x->screens[screen_id]->data = NULL;
		}
		free(x->screens[screen_id]);
		x->screens[screen_id] = NULL;
	}
	
	if( x->frames[ screen_id ] ) {
		free(x->frames[screen_id]);
		x->frames[screen_id] = NULL;
	}
}
/**
 * free splitted screens
 */
void vj_split_free( void *ptr )
{
	vj_split_t *x = (vj_split_t*) ptr;
	int i;
	for( i = 0; i < x->n_screens; i ++ ) {
		vj_split_free_screen( x, i );
	}
	free(x->frames);
	free(x->screens);
	free(x);

	x = NULL;
}

static int vj_split_shm_get( vj_split_shm_t *shm )
{
	return shm->key;
}

/**
 * create a new splitted screen
 * wid,hei,fmt are from input source
 */
static int vj_split_allocate_screen( void *ptr, int screen_id, int wid, int hei,int fmt )
{
	vj_split_t *x = (vj_split_t*) ptr;
	vj_split_free_screen(x, screen_id);

	v_screen_t *box = (v_screen_t*) vj_calloc(sizeof(v_screen_t));
	if( box == NULL )
		return 0;

	x->screens[screen_id] = box;

	VJFrame *dst = yuv_yuv_template( NULL, NULL, NULL, wid,hei,fmt );
	if( dst == NULL ) {
		return 0;
	}
	
	box->data = (uint8_t*) vj_malloc( sizeof(uint8_t) * (dst->len*4));

	dst->data[0] = box->data;
	dst->data[1] = box->data + dst->len;
	dst->data[2] = box->data + dst->len + dst->len;

	vj_frame_clear1( dst->data[0], 0, dst->len );
	vj_frame_clear1( dst->data[1], 128, dst->len + dst->len );

	x->frames[ screen_id ] = dst;

	return 1;
}

/**
 * configure this screen
 *
 * screen may edge blend by edge_x x edge_y
 * screen is w x h pixels in size
 * FIXME: x,y unused
 */
int vj_split_configure_screen( void *ptr, int screen_id, int edge_x, int edge_y, int left, int right, int top, int bottom, int w, int h )
{	
	vj_split_t *x = (vj_split_t*) ptr;
	v_screen_t *box = x->screens[screen_id];
	
	box->edge_h = edge_x;
	box->edge_v = edge_y;	
	box->width = w;
	box->height = h;

	box->left = left;
	box->right= right;
	box->top = top;
	box->bottom = bottom;

	return 1;
}

static char *get_self(void)
{
	char *path = vj_malloc( 1024 );
	if( path == NULL )
		return NULL;
	if( readlink("/proc/self/exe", path, 1024 ) == -1 ) {
		free(path);
		return NULL;
	}
	return path;
}
/**
 * get shared memory key
 */
static key_t vj_split_suggest_key(int screen_id)
{
	char *progname = get_self();
	if(progname == NULL)
		return 0;

	key_t key = ftok( progname, screen_id );
	free(progname);
	return key;
}

/**
 * Add a new splitted screen
 *
 * This routine queries another veejay for its output dimensions and allocates a new screen
 * with this information. 
 *
 */
int	vj_split_add_screen( void *ptr, char *hostname, int port, int row, int col, int out_w, int out_h, int fmt )
{
	vj_split_t *x = (vj_split_t*) ptr;
	int use_shm = 0;

	if( strcmp(hostname,"localhost") == 0 || strcmp( hostname,LOCALHOST)== 0 ) {
		use_shm = 1;
	}

	int w = 0, h = 0, format = 0, rkey = 0;
	int screen_id = (x->columns * row) + col;

	int ret = vj_share_get_info( hostname, port, &w, &h, &format, &rkey, screen_id );
	if( ret == 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to get screen info from veejay on port %d", port );
		return 0;
	}

	ret = vj_split_allocate_screen( ptr, screen_id, w, h, fmt );
	if(ret == 0) {
		veejay_msg(VEEJAY_MSG_ERROR,"Screen %d configuration error", screen_id );
		return 0;
	}
	else {
		x->current_id = screen_id;
	}

	x->off_y = (h * row);
	x->off_x = (w * col);

	int left = x->off_x;
	int right = left + w;
	int top = x->off_y;
	int bottom = top + h;
	key_t key = 0;

	if( use_shm ) {
		v_screen_t *box = x->screens[ screen_id ];
		box->shm = vj_split_shm_new( vj_split_suggest_key(screen_id), w, h );
		if(box->shm == NULL ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to setup shared resource with %s:%d",hostname,port );
			vj_split_free_screen( ptr, screen_id );
			return 0;
		}
	
		key = vj_split_shm_get( (vj_split_shm_t*) box->shm );
		
		if( vj_share_start_slave( hostname, port, key) == 0 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to communicate with %s:%d", hostname,port );
			vj_split_free_screen( ptr, screen_id );
			return 0;
		}
	}
	else {
		v_screen_t *box = x->screens[ screen_id ];
		box->net = vj_split_net_new( hostname, port, w, h );
		if(box->net == NULL ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to setup networked resource with %s:%d", hostname,port );
			vj_split_free_screen(ptr, screen_id);
			return 0;
		}
		
		if( vj_share_start_net( hostname,port, server_ip, server_port ) == 0 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to communicate with %s:%d", hostname,port );
			vj_split_free_screen( ptr, screen_id );
			return 0;
		}
	}

	veejay_msg(VEEJAY_MSG_INFO,
			"Screen #%d configuration:",screen_id);
	veejay_msg(VEEJAY_MSG_INFO,"\tSize: %dx%d", w,h);
	veejay_msg(VEEJAY_MSG_INFO,"\tType: %s", (use_shm == 1 ? "shared memory" : "network" ));
	if(use_shm) {
		veejay_msg(VEEJAY_MSG_INFO,"\tshm_id: %d (%x)", key,key);
	}
	veejay_msg(VEEJAY_MSG_INFO,"\tHost: %s:%d", hostname,port );
	veejay_msg(VEEJAY_MSG_INFO,"\tRegion: x0=%d,x1=%d y0=%d,y1=%d",left,right,top,bottom );

	return vj_split_configure_screen( ptr, screen_id, 0,0,left,right,top,bottom,w,h );
}

/**
 * copy rectangle [left,right,top,bottom]
 */
#ifdef HAVE_ASM_MMX
static void vj_split_copy_plane_mmx( uint8_t *D, uint8_t *S, int left, int right, int top, int bottom, int w, int h )
{
	int y = top;
	int nxt = 0;

	for( y = top; y < bottom; y ++ ) {
	
		uint8_t *from  = S + (y * w + left);
		uint8_t *to = D + nxt;
		int len = (right - left);
		int i = len >> 6;
		len &= 63;
		
		for( ; i > 0; i -- ) {
			__asm__ __volatile__ (
				"movq (%0), %%mm0\n"
				"movq 8(%0), %%mm1\n"
				"movq 16(%0), %%mm2\n"
				"movq 24(%0), %%mm3\n"
				"movq 32(%0), %%mm4\n"
				"movq 40(%0), %%mm5\n"
				"movq 48(%0), %%mm6\n"
				"movq 56(%0), %%mm7\n"
				"movq %%mm0, (%1)\n"
				"movq %%mm1, 8(%1)\n"
				"movq %%mm2, 16(%1)\n"
				"movq %%mm3, 24(%1)\n"
				"movq %%mm4, 32(%1)\n"
				"movq %%mm5, 40(%1)\n"
				"movq %%mm6, 48(%1)\n"
				"movq %%mm7, 56(%1)\n"

				:: "r" (from), "r" (to) : "memory" );
			
				from += 64;
				to += 64;
		}

		__asm__ __volatile__( "emms" ::: "memory" );

		if( len ) {
			register uintptr_t dummy;
			__asm__ __volatile__(
					"rep; movsb"
					: "=&D"(to),"=&S"(from),"=&c"(dummy)
					: "0"(to),"1"(from),"2"(len)
					: "memory" );
		}
	
		nxt += (right-left);
	}
}
#endif

static void vj_split_copy_plane( uint8_t *D, uint8_t *S, int left, int right, int top, int bottom, int w, int h )
{
#ifndef HAVE_ASM_MMX
	int y = top;
	int dh = bottom;
	int nxt = 0;
	int x = 0;
	for( y = top; y < dh; y ++ ) {
		for( x = left; x < right; x ++ ) {
			D[nxt] = S[ y * w + x];
			nxt++;
		} 
	}
#else
	vj_split_copy_plane_mmx( D,S,left,right,top,bottom,w,h );
#endif
}

/**
 * cut a region of interest to a new image
 */
static void vj_split_copy_region( VJFrame *src, VJFrame *dst, v_screen_t *box )
{
	vj_split_copy_plane( dst->data[0], src->data[0], box->left, box->right, box->top, box->bottom,
						 src->width, src->height );
	vj_split_copy_plane( dst->data[1], src->data[1], box->left, box->right, box->top, box->bottom,
						 src->width, src->height );
	vj_split_copy_plane( dst->data[2], src->data[2], box->left, box->right, box->top, box->bottom,
						 src->width, src->height ); 
	//FIXME alpha channel support
}

/**
 * lock shared resource, copy pixel blob, unlock shared resource
 */
static int vj_split_push_shm( v_screen_t *screen, VJFrame *frame)
{
	vj_split_shm_t *shm = (vj_split_shm_t*) screen->shm;

	unsigned char *addr = (unsigned char*) shm->sms;
	vj_shared_data *data = (vj_shared_data*) shm->sms;

	int res = pthread_rwlock_wrlock( &(data->rwlock) );
	if( res == -1 ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to acquire lock on shared resource");
		return 0;
	} 

	if( data->header[1] != screen->height || data->header[0] != screen->width ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Shared resource does not have matching video resolution");
		pthread_rwlock_unlock( &(data->rwlock) );
		return 0;
	}

	uint8_t *offset = addr + SHM_ADDR_OFFSET;
	uint8_t *Y      = offset;
	uint8_t *U      = Y + (screen->width * screen->height);
	uint8_t *V      = U + (screen->width * screen->height);

	veejay_memcpy( Y, frame->data[0], frame->len );
	veejay_memcpy( U, frame->data[1], frame->len );
	veejay_memcpy( V, frame->data[2], frame->len );
	//FIXME alpha channel support

	res = pthread_rwlock_unlock( &(data->rwlock));
	if( res == -1 ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Failed to unlock shared resource" );
		return 0;
	}

	return 1;
}

/**
 * split the image in src to N screens
 */
void vj_split_process( void *ptr, VJFrame *src )
{
	vj_split_t *x = (vj_split_t*) ptr;
	int i;

	for( i = 0; i < x->n_screens; i ++ ) {
		if( x->frames[i] == NULL || x->screens[i] == NULL )
			continue;

		vj_split_copy_region( src, x->frames[i], x->screens[i] );

		if( x->screens[i]->net ) {
			vj_split_net_t *net = (vj_split_net_t*) x->screens[i]->net;
			if(net->started == 0) {
				net->started = vj_share_play_last( net->hostname, net->port );
			}
		}
	} 
}

/**
 * write the screens to the clients
 */
void vj_split_render( void *ptr )
{
	vj_split_t *x = (vj_split_t*) ptr;
	int i;

	for( i = 0; i < x->n_screens; i ++ ) {
		if( x->frames[i] == NULL || x->screens[i] == NULL )
			continue;
		VJFrame *dst = x->frames[i];
			
		if( x->screens[i]->shm ) {
			if( vj_split_push_shm( x->screens[i], dst ) == 0 ) { //FIXME alpha channel support in vj-shm
				vj_split_free_screen( ptr, i );
			}
		}
	} 
}

VJFrame *vj_split_get_screen(void *ptr, int screen_id)
{
	vj_split_t *x = (vj_split_t*) ptr;
	if( x == NULL || screen_id < 0 || screen_id >= x->n_screens)
		return NULL;
	
	return x->frames[ screen_id ];
}
