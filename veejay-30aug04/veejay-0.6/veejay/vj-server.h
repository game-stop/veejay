#ifndef VJ_SERVER_H
#define VJ_SERVER_H
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>

#define VJ_PORT 3490
#define VJ_MAX_CONNECTIONS 8

typedef struct vj_server_t {
    fd_set master;
    fd_set current;
    struct sockaddr_in myself;
    struct sockaddr_in remote;
    int handle;
    int nr_of_connections;
    int nr_of_links;
} vj_server;

int vj_server_init();

vj_server *vj_server_alloc(int port, int type);
int vj_server_send(int link_id, char *buf, int len);
int vj_server_update(vj_server * vje);

void vj_server_shutdown(vj_server *vje, int type);

int vj_server_adv_poll(vj_server *vje,int *links);

void vj_server_list_clients();

int vj_server_retrieve_msg(int link_id, char *dst);

int vj_server_status_check(vj_server * vje);

int vj_server_status_send(vj_server *vje, char *buf, int len);

void vj_server_close_link(vj_server * vje, int link_id, int type);

int vj_server_poll(vj_server * vje);

int vj_server_must_send(); 

#endif
