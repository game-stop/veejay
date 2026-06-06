#ifndef VJ_JACK_H
#define VJ_JACK_H

#include <config.h>
#include <stdint.h>
#include <libel/vj-el.h>

#ifdef HAVE_JACK
int vj_jack_initialize(void);
int vj_jack_stop(void);
int vj_jack_init(editlist *el);
int vj_jack_init_input(editlist *el);
int vj_jack_init_duplex(editlist *el);
int vj_jack_init_capture(int input_channels, unsigned int bits_per_channel, unsigned long jack_port_flags);
int vj_jack_update_buffer(uint8_t *buff, int bps, int num_channels, int buf_len);
int vj_jack_get_client_samplerate(void);
int vj_jack_start(void);
long vj_jack_underruns(void);
long vj_jack_input_overruns(void);
int vj_jack_xrun_flag(void);
void vj_jack_enable(void);
void vj_jack_disable(void);
int vj_jack_c_play(void *data, int len, int entry);
int vj_jack_play(void *data, int len);
int vj_jack_play2(void *data, int len);
int vj_jack_read(void *data, int len);
int vj_jack_capture_read(void *data, int len);
int vj_jack_get_rate(void);
int vj_jack_set_volume(int volume);
void vj_jack_flush(void);
int vj_jack_pause(void);
int vj_jack_resume(void);
int vj_jack_get_space(void);
long vj_jack_get_status(long int *sec, long int *nsec);
double vj_jack_get_played_position(void);
unsigned long vj_jack_get_played_frames(void);
long vj_jack_get_written_frames(void);
void vj_jack_reset(void);
void vj_jack_reset_input(void);
int vj_jack_is_starving(void);
int vj_jack_get_period_size(void);
int vj_jack_get_ringbuffer_frames_free(void);
int vj_jack_get_ringbuffer_size(void);
long vj_jack_get_ringbuffer_used(void);
int vj_jack_client_to_jack_frames(int client_frames);
long vj_jack_get_required_free_frames(int client_frames);
unsigned long vj_jack_get_bytes_per_frame(void);
unsigned long vj_jack_get_bytes_per_input_frame(void);
unsigned long vj_jack_get_input_bytes_per_second(void);
long vj_jack_get_bytes_stored_from_driver(void);
long vj_jack_get_input_bytes_stored(void);
int vj_jack_is_stopped(void);
int vj_jack_is_playing(void);
int vj_jack_is_callback_active(void);
double vj_jack_get_total_latency(void);
void vj_jack_set_input_passthrough(int enabled);
int vj_jack_get_input_passthrough(void);
int vj_jack_is_running(void);
int vj_jack_has_input(void);
int vj_jack_has_output(void);
int vj_jack_get_input_channels(void);
int vj_jack_get_output_channels(void);
#endif

#endif
