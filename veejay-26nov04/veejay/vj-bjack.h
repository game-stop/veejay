#ifndef VJ_BIOJACK_H
#define VJ_BIOJACK_H

int	vj_jack_initialize();
int 	vj_jack_init(int rate_hz, int channels, int bps);
int	vj_jack_stop();
int	vj_jack_reset();
int	vj_jack_play(void *data, int len);
int	vj_jack_pause();
int	vj_jack_resume();
int	vj_jack_get_space();
long 	vj_jack_get_status(long int *sec, long int *usec);
int	vj_jack_set_volume(int v);
int	vj_jack_c_play(void *data, int len, int entry);

#endif 
