#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "vj-server.h"
#include "vj-global.h"
#include "vj-common.h"
/* todo : better server implementation
	 V0004DxxxxxxSUMV

   OSC 
   
*/

typedef struct {
    char header[1];
    int data_len;
    char data[1];
    char message[MESSAGE_SIZE];
} vj_message;

typedef struct {
    int handle;
    int in_use;
    vj_message **m_queue;
} vj_link;

typedef struct {
    int handle;
    int in_use;
} vj_slink;

#define VJ_MAX_PENDING_MSG 64
static vj_link **Link;
static vj_slink **StatusLink;

static int m_queue_write = 0;
static int m_queue_read = 0;

int vj_server_is_connected(int link_id) {
	return (Link[link_id]->in_use);
}

void vj_server_list_clients()
{
    int i;
    for (i = 0; i < VJ_MAX_CONNECTIONS; i++) {
	if (Link[i]->in_use == 1) {
	    if(StatusLink[i]->in_use==1) {
		veejay_msg(VEEJAY_MSG_WARNING, "Link %d in use, processes status information.",i);     
		}
		else {
		veejay_msg(VEEJAY_MSG_WARNING, "Link %d in use, no status processing.",i);
		}
	} else {
	    veejay_msg(VEEJAY_MSG_DEBUG, "Link %d is available", i);
	}
    }
}

vj_server *vj_server_alloc(int port, int type)
{
    int i;
    int on = 1;
    int flags;
    vj_server *vjs = (vj_server *) vj_malloc(sizeof(struct vj_server_t));
    if (!vjs)
	return NULL;

    FD_ZERO(&(vjs->master));
    FD_ZERO(&(vjs->current));

    /* listener */
    if ((vjs->handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
	veejay_msg(VEEJAY_MSG_ERROR, "Failed to create a socket");
	return NULL;
    }

    fcntl(vjs->handle, F_GETFL, &flags);
    //flags |= O_NONBLOCK;
    //fcntl(vjs->handle, F_SETFL, &flags);

    if ((setsockopt
	 (vjs->handle, SOL_SOCKET, SO_REUSEADDR, (const char *) &on,
	  sizeof(on))) == -1) {
	veejay_msg(VEEJAY_MSG_ERROR,
		   "Cannot turn off bind addres checking");
    }

    vjs->myself.sin_family = AF_INET;
    vjs->myself.sin_addr.s_addr = INADDR_ANY;
    vjs->myself.sin_port = htons(port);
    memset(&(vjs->myself.sin_zero), '\0', 8);

    if (bind
	(vjs->handle, (struct sockaddr *) &(vjs->myself),
	 sizeof(vjs->myself)) == -1) {
	veejay_msg(VEEJAY_MSG_ERROR, "Bind error - Port %d in use ?", port);
	return NULL;
    }

    if (listen(vjs->handle, VJ_MAX_CONNECTIONS) == -1) {
	veejay_msg(VEEJAY_MSG_ERROR, "Listen error.");
	return NULL;
    }

    FD_SET(vjs->handle, &(vjs->master));
    vjs->nr_of_connections = vjs->handle;
    vjs->nr_of_links = 0;

    if (type == 0) {
	Link = (vj_link **) malloc(sizeof(vj_link *) * (VJ_MAX_CONNECTIONS+1));

	for (i = 0; i <= VJ_MAX_CONNECTIONS; i++) {
	    int j;
	    Link[i] = (vj_link *) malloc(sizeof(vj_link));
	    Link[i]->in_use = 0;
	    Link[i]->m_queue =
		(vj_message **) malloc(sizeof(vj_message *) *
				       (VJ_MAX_PENDING_MSG + 1));
	    if(!Link[i]->m_queue) return NULL;
	    for (j = 0; j < (VJ_MAX_PENDING_MSG + 1); j++) {
		Link[i]->m_queue[j] =
		    (vj_message *) malloc(sizeof(vj_message));
	        if(!Link[i]->m_queue[j]) return NULL;
	        Link[i]->m_queue[j]->data_len = 0;
	        bzero(Link[i]->m_queue[j]->message,0);
		Link[i]->m_queue[j]->data[0] = 0;
		Link[i]->m_queue[j]->header[0] = 0;
	    }
	}
	veejay_msg(VEEJAY_MSG_INFO, "Command socket ready at port %d", port);
    }
    if (type == 1) {
	StatusLink =
	    (vj_slink **) malloc(sizeof(vj_slink *) * (VJ_MAX_CONNECTIONS+1));
	for (i = 0; i <= VJ_MAX_CONNECTIONS; i++) {
	    
	    StatusLink[i] = (vj_slink *) malloc(sizeof(vj_slink));
	    StatusLink[i]->in_use = 0;
	}
	veejay_msg(VEEJAY_MSG_INFO, "Status socket ready at port %d", port);
    }
    return vjs;
}

int vj_server_send(int link_id, char *buf, int len)
{
    unsigned int total = 0;
    unsigned int bytes_left = len;
    int n;
    if (len <= 0)
	return -1;
    while (total < len) {
	n = send(Link[link_id]->handle, buf + total, bytes_left, 0);
	if (n == -1)
	    return -1;
	total += n;
	bytes_left -= n;
    }
    return 0;
}


int _vj_server_free_slot(int type)
{
    int i;
    if (type == 1) {
	for (i = 0; i < VJ_MAX_CONNECTIONS; i++) {
	    if (StatusLink[i]->in_use == 0)
		return i;
	}
    }
    if (type == 0) {
	for (i = 0; i < VJ_MAX_CONNECTIONS; i++) {
	    if (Link[i]->in_use == 0)
		return i;
	}
    }
    return -1;
}
int _vj_server_new_client(int socket_fd)
{
    int entry = _vj_server_free_slot(0);
    if (entry == -1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot take more connections");
		return -1;
	}
    Link[entry]->handle = socket_fd;
    Link[entry]->in_use = 1;
   veejay_msg(VEEJAY_MSG_INFO, "Registered new client %d (socket %d)",entry,socket_fd);
    return 0;
}

int _vj_server_del_client(vj_server * vje, int link_id)
{
    if (Link[link_id]->in_use == 1) {
	Link[link_id]->in_use = 0;
	close(Link[link_id]->handle);
	FD_CLR(Link[link_id]->handle, &(vje->master));
	veejay_msg(VEEJAY_MSG_INFO,"Closed command socket with Link %d",link_id);
	return 0;
    }
    return -1;
}

int vj_server_must_send(vj_server *vje) {
	return vje->nr_of_links;
}

int vj_server_poll(vj_server * vje)
{
    struct timeval t;
    int i;
    //memset(t, 0, sizeof(struct timeval));
    t.tv_sec = 0;
    t.tv_usec = 0;
    vje->current = vje->master;
    if (select(vje->nr_of_connections + 1, &(vje->current), NULL, NULL, &t)
	== -1 )
	return 0;
    for(i=0; i <= vje->nr_of_connections; i++)
	{
		if(FD_ISSET(i, &(vje->current))) return 1;                        
	}
    return 0;
}

int vj_server_adv_poll(vj_server *vje , int *links) {
	struct timeval t;
	int i=0;
	int ret_val = 0;
	t.tv_sec = 0;
	t.tv_usec = 0;
	vje->current = vje->master;
	if(select(vje->nr_of_connections +1, &(vje->current),NULL,NULL,&t) == -1) return -1;
	for(i=0; i < vje->nr_of_links; i++)
	{
		if(FD_ISSET( Link[i]->handle,&(vje->current) ) )
		{
			links[ret_val] = i;
		}
		if(Link[i]->in_use) links[ret_val] = i;
		ret_val++;
	}
	 
	return ret_val;
}
#define RECV_SIZE (1024*256)
static unsigned char *recv_buffer;
int vj_server_init()
{
	// 1 mb buffer
	recv_buffer = (unsigned char*) malloc(sizeof(unsigned char) * RECV_SIZE);
	if(!recv_buffer) return 0;
	bzero( recv_buffer, RECV_SIZE );
	return 1;
}

static int _which_link(int socket)
{
	int i;
	for(i=0; i < VJ_MAX_CONNECTIONS;i++)
	{
		if(Link[i]->handle == socket) return i;
	}
	return -1;
}

int vj_server_update(vj_server * vje)
{
    int i;

    //if (link_id < 0 || link_id > VJ_MAX_CONNECTIONS)
    //	return -1;
    /* try new connections */
    for (i = 0; i <= vje->nr_of_connections; i++) {
	if (FD_ISSET(i, &(vje->current)) && i == vje->handle) {
	    int addr_len = sizeof(vje->remote);
	    int fd =
		accept(vje->handle, (struct sockaddr *) &(vje->remote),
		       &addr_len);
	    if (fd != -1) {
		FD_SET(fd, &(vje->master));
		if (vje->nr_of_connections < fd) {
		    vje->nr_of_connections = fd;
		}
		if (_vj_server_new_client(fd) != 0) {
		    close(fd);
		    FD_CLR(fd, &(vje->master));
		    veejay_msg(VEEJAY_MSG_ERROR,"Cannot establish connection with caller");
		}
   		
		return 0;
	    }
	} 
	else
	{
	 if(FD_ISSET(i, &(vje->current)))
	 {

	 	int id = _which_link(i);
		if ( id >= 0 && Link[id]->in_use) {
		  int n;
		  int nread = RECV_SIZE;          
		  int n_msg = 0;
		  int j = 0;
		  m_queue_write = 0;
		  m_queue_read = 0;
		  /* try to read lots of bytes bytes */
		  n = recv(Link[id]->handle, recv_buffer, nread, 0);
		  if(n<=0)
		  {
			veejay_msg(VEEJAY_MSG_ERROR, "Closing connection with %d (recv <= 0)",
				i);
			_vj_server_del_client(vje,id);
			return -1;
		  }
		  while(j < n) {
			vj_message *raw	= Link[id]->m_queue[m_queue_write];
			if(recv_buffer[j] == 'V' && recv_buffer[j+4] == 'D')
			{
				int m_len = 0;
				if(sscanf(recv_buffer+(j+1), "%03d",&m_len)>0)
				{
				 j+=5; /* skip header */
				 if(m_queue_write < VJ_MAX_PENDING_MSG)
				  {
				 raw->header[0] = recv_buffer[0];
				 raw->data[0] = recv_buffer[4];
				 raw->data_len = m_len;
				 bzero(raw->message, MESSAGE_SIZE);
				 strncpy( raw->message, recv_buffer+j, m_len );
				 j+= m_len;
				 n_msg ++;
				 m_queue_write ++;
				  }
				}
		  	}
			j++;
		}	
		bzero(recv_buffer, j);
		return n_msg;
		}

	    }
	}
    }
    return -1;
}


int vj_server_status_check(vj_server * vje)
{
    int i = 0;

    for (i = 0; i <= vje->nr_of_connections; i++) {
	if (FD_ISSET(i, &(vje->current))) {
	    if (i == vje->handle) {
		int addr_len = sizeof(vje->remote);
		int fd =
		    accept(vje->handle, (struct sockaddr *) &(vje->remote),
			   &addr_len);
		if (fd != -1) {
		    int entry = -1;
		    FD_SET(fd, &(vje->master));
		    if (vje->nr_of_connections < fd) {
			vje->nr_of_connections = fd;
		    }
		    entry = _vj_server_free_slot(1);
		    if (entry != -1) {
			StatusLink[entry]->handle = fd;
			StatusLink[entry]->in_use = 1;
			vje->nr_of_links++;
			return 0;
		    } else {
			close(fd);
			FD_CLR(fd, &(vje->master));
			vje->nr_of_links--;
			if(vje->nr_of_links<0) vje->nr_of_links=0;
		    }
		}
		return -1;
	    }
	}
    }
    return -1;
}
int vj_server_status_send(vj_server *vje, char *buf, int len)
{
    int n;
    int i;
	unsigned int total = -1;
	unsigned int bytes_left = len;

    if (len <= 0) return -1;

    for(i=0; i < VJ_MAX_CONNECTIONS; i++) {
     if (StatusLink[i]->in_use == 1 ) {
 	//if( FD_ISSET( StatusLink[i]->handle, &(vje->master)) ) {
	//}
	total = 0;
	bytes_left = len;
	while (total < len) {
	    n = send(StatusLink[i]->handle, buf + total, bytes_left,
		     0);
	    if (n <= 0) {
		close(StatusLink[i]->handle);
		StatusLink[i]->in_use = 0;
		FD_CLR( StatusLink[i]->handle, &(vje->master));
		vje->nr_of_links--;
		veejay_msg(VEEJAY_MSG_INFO, "Closed status connection with Link %d",i);
		return -1;
	    }
	    total += n;
	    bytes_left -= n;
	}
     }
    }
    return total;
}
/* close_link : close a status link (1) or normal (0) */
void vj_server_close_link(vj_server * vje, int link_id, int type)
{
    if (link_id >= 0 && link_id < VJ_MAX_CONNECTIONS) {
	if (type == 0) {
	    if (Link[link_id]->in_use == 1) {
		close(Link[link_id]->handle);
		Link[link_id]->in_use = 0;
		FD_CLR(Link[link_id]->handle, &(vje->master));
		veejay_msg(VEEJAY_MSG_INFO,"Closed command socket with link %d",link_id);
	    }
	}
	if (type == 1) {
	    if (StatusLink[link_id]->in_use == 1) {
		close(StatusLink[link_id]->handle);
		StatusLink[link_id]->in_use = 0;
		FD_CLR(StatusLink[link_id]->handle, &(vje->master));
		veejay_msg(VEEJAY_MSG_INFO,"Closed status socket with link %d",link_id);
	    }
	}

    }
}


void vj_server_shutdown(vj_server *vje, int type) {
  int link_id;
  int i;

  if(type==1) {
    for(link_id=0; link_id < VJ_MAX_CONNECTIONS; link_id++) {
	if(Link[link_id]->in_use==1) close(Link[link_id]->handle);
    }
    close(vje->handle);
    for (i = 0; i <= VJ_MAX_CONNECTIONS; i++) {
      int j;
      for(j=0; j < (VJ_MAX_PENDING_MSG+1); j++) {
     	 if(Link[i]->m_queue[j]) free(Link[i]->m_queue[j]);
      }
      if(Link[i]->m_queue) free(Link[i]->m_queue);
      if(Link[i]) free(Link[i]);
    }
    free(Link);
  }

  if(type==0) {
    for(link_id=0; link_id < VJ_MAX_CONNECTIONS; link_id++) {
	if(StatusLink[link_id]->in_use==1) close(StatusLink[link_id]->handle);
    }
    close(vje->handle);
    for (i = 0; i <= VJ_MAX_CONNECTIONS; i++) {
	    free(StatusLink[i]);
    }
   free(StatusLink);
  }
}

/* retrieve_msg:
   keep repeating this function until all messages have been parsed.
   it copies the string in the message queue to dst,
   and sets the m_queue_read ready for next call.
   return 0 on success, -1 on failure (no more messages) */
int vj_server_retrieve_msg(int link_id, char *dst)
{
    if (Link[link_id]->in_use == 0)
	return -1;

    if (m_queue_read < m_queue_write) {
	if (Link[link_id]->m_queue[m_queue_read]->data_len > 0) {
	    strncpy(dst,
		    Link[link_id]->m_queue[m_queue_read]->message,
		    Link[link_id]->m_queue[m_queue_read]->data_len);
	    dst[Link[link_id]->m_queue[m_queue_read]->data_len] = '\0';
	    Link[link_id]->m_queue[m_queue_read]->data_len = 0;
	    bzero(Link[link_id]->m_queue[m_queue_read]->message,
		  MESSAGE_SIZE);
	    m_queue_read ++;
	    return (m_queue_write - m_queue_read );
	}
    }
    return -1;			/* no messages */
}
