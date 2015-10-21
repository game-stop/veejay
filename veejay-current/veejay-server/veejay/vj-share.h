#ifndef VJSHARE
#define VJSHARE
int32_t vj_share_pull_master( void *shm, char *master_host, int master_port );
int	vj_share_get_info( char *host, int port, int *width, int *height, int *format, int *key, int screen_id );
int	vj_share_start_slave( char *host, int port, int shm_id);

int	vj_share_start_net( char *host, int port, char *master_host, int master_port);
int	vj_share_play_last( char *host, int port );

#endif

