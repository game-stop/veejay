#ifndef VJAUDIO_H
#define VJAUDIO_H
int audio_init(int rate, int channels,char *port_name, char *client_name);
void audio_uninit(int immed);
void audio_reset(void);
void audio_pause(void);
void audio_resume(void);
int audio_play(void *data, int len, int flags);
float audio_get_delay(struct timeval bs);
void	audio_continue(int speed);
int	audio_get_buffered_bytes(long *sec, long *usec);
#endif
