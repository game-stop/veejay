/* sendVIMS - very simple client for VeeJay
 * 	     (C) 2002-2004 Niels Elburg <elburg@hio.hen.nl>
 *
 * puredata module by Tom Schouten <doelie@zzz.kotnet.org>
 *
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <setjmp.h>

#include "m_pd.h"


#define POLL_INTERVAL (1.0f) // agressive polling
#define MAX_MSG 256          // maximum message size (bytes)

#define QUEUE_SIZE (1<<8)    // message queue size (power of 2)
#define QUEUE_MASK (QUEUE_SIZE - 1)


// some symbols used in comm
// (gensym not thread safe)
static t_symbol *s_disconnect = 0;
static t_symbol *s_veejay = 0;

static t_symbol *selector[602];

/* DATA STRUCTURES */

typedef struct {
    struct hostent *he;
    struct sockaddr_in server_addr;
    int handle;
} veejay_t;


typedef struct {
    int delay; // nb of frames to delay this msg
    char msg[MAX_MSG - sizeof(int)];
} vj_msg_t;

typedef struct {
    t_symbol *selector;
    int argc;
    t_atom argv[(MAX_MSG / sizeof(t_atom)) - sizeof(t_symbol *) - sizeof(int)];
} pd_msg_t;


typedef struct {
    void *queue[QUEUE_SIZE];
    unsigned int read;
    unsigned int write;
} queue_t;

typedef struct {
    t_object obj;
    t_outlet *outlet;

    /* network */
    t_symbol *hostname;
    int port;
    veejay_t status_socket;
    veejay_t command_socket;

    /* message queues */
    queue_t vq; /* pd -> veejay */
    queue_t pq; /* veejay -> pd */
    t_clock *clock; // polling clock
     
    /* thread */
    pthread_t thread;
    jmp_buf errorhandler;

    /* obj status */
    int connected;      // socket connected
    int run;            // communication thread running

} sendVIMS_t;






/* CODE */


/* messages */

// free message (pd_msg_t and vj_msg_t)
void msg_free(void *m){
    if (!m) return;
    free(m);
}

// map symbolic selector to numeric veejay id
int selector_map(t_symbol *s){
    int i;
    
    // map p<num> syms
    if (s->s_name[0] == 'p'){
	return atoi(s->s_name + 1);
    }

    // check the stuff from selectors.h
    for (i=0; i<602; i++){
	if (s == selector[i]) return i;
    }

    // fallthrough
    post("sendVIMS: selector %d not recognized", s->s_name);
    return 0;
}

void setup_selectors(void){
    memset(selector, 0, sizeof(selector));
#define SELECTOR(name, id) selector[id] = gensym(name)
#include "selectors.h"
#undef SELECTOR
}




// create a pd message from a veejay message
// will be zero terminated (extra check)


pd_msg_t *pd_msg_new(char *msg){
    int i, parsed, size = -1;
    char *body = msg + 5;
    pd_msg_t *m = NULL;
    int s[27]; 
    int n = 0;
	
    /* get 26 ints */
    n = sscanf(body, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
		s+0, s+1, s+2, s+3,
		s+4, s+5, s+6, s+7,
		s+8, s+9, s+10, s+11,
		s+12,s+13,s+14, s+15,
		s+16,s+17,s+18, s+19,
		s+20,s+21,s+22, s+23,
	       	s+24,s+25);

    if( n != 26 )
	    goto error;

    /* create msg */
    size_t est = n * sizeof(float) + sizeof(pd_msg_t); //@ malloc(sizeof(*m)) not ok
    m = malloc(est);
    m->selector = s_veejay; // not used
    m->argc = n;
    for(i=0; i<n; i++) SETFLOAT(m->argv + i, (float)s[i]);
    return m;

  error:
    if( m )
    	msg_free(m);
    post("Parsed only %d out of %d status outlets", n,26 );
    return 0;

}


// create veejay message from a pd message
vj_msg_t *vj_msg_new(t_symbol *selector, int argc, t_atom *argv){
    vj_msg_t *m = malloc(sizeof(*m));
    char *body, *c = m->msg;
    m->delay = 0;
    m->msg[0] = 0;

    // the format of a message is simple
    // if the first argument is a "+" the next argument (float) 
    // will be interpreted as a frame delay

    if (selector == gensym("+")){

	// get delay
	if (!argc) goto error;
	if (argv->a_type != A_FLOAT) goto error;
	m->delay = (int)argv->a_w.w_float;
	argc--, argv++;

	// get real selector
	if (!argc) goto error;
	if (argv->a_type != A_SYMBOL) goto error;
	selector = argv->a_w.w_symbol;
	argc--, argv++;
    }
    
    // the rest is interpreted as a veejay message

    // leave space for data header
    c += sprintf(c, "V000D");  body = c; 

    // map selector
    c += sprintf(c, "%03d:", selector_map(selector));
    
    // print args
    while (argc){
		switch(argv->a_type){
		case A_SYMBOL:
				c += sprintf(c, "%s", argv->a_w.w_symbol->s_name);
				if(argc > 1)
						c += sprintf(c, "%s", " ");
				
				break;
		case A_FLOAT:
				c += sprintf(c, "%d", (int)argv->a_w.w_float);
				if(argc > 1)
						c += sprintf(c, "%s", " ");

				break;
		default:
		    goto error;
		}
		argc--,argv++;
    }
	c += sprintf(c, ";");
    sprintf(m->msg, "V%03d", strlen(body)); // fill header
    m->msg[4] = 'D';
    return m;

  error:
    post("sendVIMS: parse error");
    msg_free(m);
    return 0;
}



/* queues */


void queue_write(queue_t *x, void *m){
    int messages = (x->write - x->read) & QUEUE_MASK;
    if (messages == QUEUE_MASK){
	post("sendVIMS: message queue full: ignoring command");
	free(m);
    }
    else {
	x->queue[x->write] = m;
	x->write = (x->write + 1) & QUEUE_MASK;
    }
}

void *queue_read(queue_t *x){
    void *msg = 0;
    int messages = (x->write - x->read) & QUEUE_MASK;
    if (!messages) return 0;
    else {
	msg = x->queue[x->read];
	x->read = (x->read + 1) & QUEUE_MASK;
    }
    return msg;
}

void queue_init(queue_t *x){
    memset(x, 0, sizeof(queue_t));
}


/* pd object */


// queue  pd -> veejay 
void sendVIMS_vq_write(sendVIMS_t *x, vj_msg_t *m){ queue_write(&x->vq, (void *)m); }
vj_msg_t  *sendVIMS_vq_read(sendVIMS_t *x){ return (vj_msg_t *)queue_read(&x->vq); }

// queue veejay -> pd
void sendVIMS_pq_write(sendVIMS_t *x, pd_msg_t *m){ queue_write(&x->pq, (void *)m); }
pd_msg_t  *sendVIMS_pq_read(sendVIMS_t *x){ return (pd_msg_t *)queue_read(&x->pq); }


// connect a veejay port
static int vj_connect(veejay_t *v, char *name, int port_id ) {
    v->he = gethostbyname(name);
    v->handle = socket( AF_INET, SOCK_STREAM, 0);
    v->server_addr.sin_family = AF_INET;
    v->server_addr.sin_port = htons(port_id);
    v->server_addr.sin_addr = *( (struct in_addr*) v->he->h_addr);
    if(connect(v->handle, (struct sockaddr*)
	       &v->server_addr,sizeof(struct sockaddr))==-1) return -1; /* error */
    return 0;
}


// send disconnect command from thread
static void sendVIMS_disconnect_from_thread(sendVIMS_t *x){
    pd_msg_t *m = malloc(sizeof(*m));
    m->selector = s_disconnect;
    m->argc = 0;
    sendVIMS_pq_write(x, m);        // send close command
    longjmp(x->errorhandler, -1);   // jump to thread error handler
}

// read one chunk of status information
static pd_msg_t *sendVIMS_status(sendVIMS_t *x) {
    int gotbytes = 0;
    int wantbytes = 0;
    pd_msg_t *m = 0;
    char buf[100];
    int size = -1;

    // read header
    wantbytes = 5;
    gotbytes  = recv(x->status_socket.handle, buf, wantbytes, 0);
    if (wantbytes != gotbytes) goto error;
    if (1 != sscanf(buf, "V%03dD", &size)) goto proto_error;

    // read body
    wantbytes = size;
    gotbytes  = recv(x->status_socket.handle, buf + 5, wantbytes, 0);
    if (wantbytes != gotbytes) goto error;

    // return a pd message
    return pd_msg_new(buf);

  error:
    if (gotbytes > 0) {
	post("sendVIMS: message truncated: wanted %d bytes, got %d", 
	     wantbytes, gotbytes);
    }
    else if (gotbytes == 0) {
	post("sendVIMS: remote end closed connection");
    }
    else {
	perror("sendVIMS");
    }
    return 0;

  proto_error:
    post("sendVIMS: protocol error: not a valid veejay header.");
    return 0;

}

// flush and get status messages
static void sendVIMS_flush(sendVIMS_t *x, int frames) {
    pd_msg_t *m;
    int n = 0;
    while (frames--){
	m = sendVIMS_status(x); // get status message
	if (!m) {
	    // disconnect on error
	    sendVIMS_disconnect_from_thread(x);
	    return;
	}
    	sendVIMS_pq_write(x, m); // write it to queue
    }
}

// send a raw message to veejay
static void sendVIMS_send(sendVIMS_t *x, char *buf) {

    //post("sending msg: '%s'", buf);
    if ((send(x->command_socket.handle, buf, strlen(buf), 0)) == -1)
	{ /* send the command */
            perror("sendVIMS: can't send: ");
	    sendVIMS_disconnect_from_thread(x);
	}
}


// init struct with defaults
static void sendVIMS_init(sendVIMS_t *x){
    memset(&x->status_socket, 0, sizeof(veejay_t));
    memset(&x->command_socket, 0, sizeof(veejay_t));
    queue_init(&x->vq);
    queue_init(&x->pq);
    x->hostname = gensym("localhost");
    x->port = 3490;
    x->connected = 0;
    x->run = 0;
}

// disco vj
static void sendVIMS_disconnect(sendVIMS_t *x){

    void *m = 0;

    // stop thread
    if (x->run){
	x->run = 0;
	pthread_join(x->thread, 0);
    }

    // close socket
    if (x->connected){
        close(x->status_socket.handle);
	close(x->command_socket.handle);
	x->connected = 0;
	post("sendVIMS: disconnected.");
    }

    // clear vq & pq
    while (m = sendVIMS_vq_read(x)) msg_free(m);
    while (m = sendVIMS_pq_read(x)) msg_free(m);
}

// veejay command thread
static void sendVIMS_thread(sendVIMS_t *x){
    vj_msg_t *m;
    
    /* install error handler:
       just terminate thread (wait for join) */
    if (setjmp(x->errorhandler)) return;

    /* sync to veejay */
    while (x->run){

	/* perform all commands in vq */
	while (m = sendVIMS_vq_read(x)){
	    sendVIMS_flush(x, m->delay);      // sync to next frame
	    sendVIMS_send(x, m->msg); // send command
	}

	/* sync to next frame */
	sendVIMS_flush(x, 1);
    }
}



// co vj
static void sendVIMS_connect(sendVIMS_t *x, t_symbol *host, t_float fport){
    pthread_attr_t attr;
    int port = (int)fport;
    if (!port) port = 3490; // default veejay port

    /* connect */
    sendVIMS_disconnect(x); // disco first
    if ((vj_connect(&x->command_socket, host->s_name, port)) == -1){
	post("sendVIMS: can't connect to veejay at %s:%d (command port)", host->s_name, port);
	goto error;
    }
    if ((vj_connect(&x->status_socket, host->s_name, port+1)) == -1){
	post("sendVIMS: can't connect to veejay at %s:%d (status port)", host->s_name, port);
	goto error;
    }
    x->connected = 1;

    /* start thread */
    x->run = 1;
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
    pthread_create(&x->thread, &attr, (void* (*)(void *))sendVIMS_thread, x);

    /* done */
    post("sendVIMS: connected to %s:%d", host->s_name, port);
    return;

  error:
    sendVIMS_disconnect(x);
    return;

}


static void sendVIMS_reconnect(sendVIMS_t *x){
    sendVIMS_disconnect(x);
    sendVIMS_connect(x, x->hostname, x->port);
}

// pd input message handler
static void sendVIMS_anything(sendVIMS_t *x, t_symbol *selector, int argc, t_atom *argv){
    vj_msg_t *m = 0;
    if (!x->connected) 	post("sendVIMS: not connected to veejay.");
    else if (m = vj_msg_new(selector, argc, argv)) sendVIMS_vq_write(x, m);
}


t_class *sendVIMS_class = 0;

static void sendVIMS_free(sendVIMS_t *x){
    sendVIMS_disconnect(x);
    clock_free(x->clock);
}

// pd queue poller
static void sendVIMS_tick(sendVIMS_t *x){
    pd_msg_t *m;
    clock_delay(x->clock, POLL_INTERVAL);

    while (m = sendVIMS_pq_read(x)){
	if (m->selector == s_veejay){
	    outlet_anything(x->outlet, gensym("list"), m->argc, m->argv);
	}
	else if (m->selector == s_disconnect){
	    sendVIMS_disconnect(x);
	}
	msg_free(m);
    }
}

static void *sendVIMS_new(t_symbol *moi, int argc, t_atom *argv){
    sendVIMS_t *x = (sendVIMS_t *)pd_new(sendVIMS_class);
    sendVIMS_init(x);

    x->clock = clock_new(x, (t_method)sendVIMS_tick);
    x->outlet = outlet_new(&x->obj, gensym("anything"));

    if ((argc >= 1) && argv[0].a_type == A_SYMBOL){
	x->hostname = argv[1].a_w.w_symbol;
    }
    if ((argc >= 2) && argv[1].a_type == A_FLOAT){
	x->port = (int)argv[1].a_w.w_float;
    }
    sendVIMS_connect(x, x->hostname, x->port);

    sendVIMS_tick(x); // start clock

    return (void *)x;
}

static void post_selectors(void){
    int i;
    for (i=0; i<600; i++){
	if (selector[i]){
	    post("p%03d = %s", i, selector[i]->s_name);
	}
    }
}

void sendVIMS_setup(void){
    post("sendVIMS: version " VERSION);
    post("sendVIMS: (c) 2004-2006 Niels Elburg & Tom Schouten");
    post("sendVIMS: assuming veejay-0.9.8");

    s_disconnect = gensym("disconnect");
    s_veejay     = gensym("veejay");
    setup_selectors();

    sendVIMS_class = class_new(gensym("sendVIMS"), (t_newmethod)sendVIMS_new,
    	(t_method)sendVIMS_free, sizeof(sendVIMS_t), 0, A_GIMME, 0);
    class_addanything(sendVIMS_class, (t_method)sendVIMS_anything);
    class_addmethod(sendVIMS_class, (t_method)sendVIMS_reconnect,  gensym("reconnect"), 0);
    class_addmethod(sendVIMS_class, (t_method)sendVIMS_disconnect, gensym("disconnect"), 0);
    class_addmethod(sendVIMS_class, (t_method)sendVIMS_connect,    gensym("connect"), A_SYMBOL, A_DEFFLOAT, 0);

    class_addmethod(sendVIMS_class, (t_method)post_selectors, gensym("aliases"), 0);

}
