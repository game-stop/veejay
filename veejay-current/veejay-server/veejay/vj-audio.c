/*
 * JACK audio output driver for MPlayer
 *
 * Copyleft 2001 by Felix Bünemann (atmosfear@users.sf.net)
 * and Reimar Döffinger (Reimar.Doeffinger@stud.uni-karlsruhe.de)
 *
 * This file is part of MPlayer.
 * Modified by Niels Elburg for the Veejay project, April 2009
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * along with MPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
/*
 * a very simple circular buffer FIFO implementation
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2006 Roman Shaposhnik
 *
 * This file is part of FFmpeg.
 */

#include <config.h>
#ifdef HAVE_JACK
#include <stdint.h>
#include <jack/jack.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <veejay/vj-audio.h>
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))

#define MAX_CHANS 2
#define CHUNK_SIZE (16*1024)
#define NUM_CHUNKS 8
#define BUFFSIZE (NUM_CHUNKS * CHUNK_SIZE)
typedef struct AVFifoBuffer {
    uint8_t *buffer;
    uint8_t *rptr, *wptr, *end;
    uint32_t rndx, wndx;
} AVFifoBuffer;

struct deinterleave {
  float **bufs;
  int num_bufs;
  int cur_buf;
  int pos;
};

static jack_port_t *ports[MAX_CHANS];
static int	     num_ports = 0;
static jack_client_t *client = NULL;
static	float	     latency =0;
static	int	     estimate = 1;
static  int	     bps =  0;
static  int	     audio_rate  = 0;
static	int	     paused = 0;
static	int	     underrun = 0;
static	float	     callback_interval = 0;
static	float	     callback_time = 0;
static	AVFifoBuffer	*buffer = NULL;
static	struct	     timeval callbackTime;
static  long	     bytes_buffered = 0;

static void av_fifo_reset(AVFifoBuffer *f);
static int av_fifo_generic_read(AVFifoBuffer *f, void *dest, int buf_size, void (*func)(void*, void*, int));
static void av_fifo_drain(AVFifoBuffer *f, int size);
static void silence(float **bufs, int cnt, int num_bufs);
static unsigned int GetTimer(void);
static int usec_sleep(int usec_delay);

static AVFifoBuffer *av_fifo_alloc(unsigned int size)
{
    AVFifoBuffer *f= (AVFifoBuffer*)vj_calloc(sizeof(AVFifoBuffer));
    if(!f)
        return NULL;
    f->buffer = (uint8_t*)vj_malloc(size);
    f->end = f->buffer + size;
    av_fifo_reset(f);
    if (!f->buffer)
        av_freep(&f);
    return f;
}

static inline uint8_t av_fifo_peek(AVFifoBuffer *f, int offs)
{
    uint8_t *ptr = f->rptr + offs;
    if (ptr >= f->end)
        ptr -= f->end - f->buffer;
    return *ptr;
}
static void av_fifo_free(AVFifoBuffer *f)
{
    if(f){
        free(f->buffer);
        free(f);
    }
}
static void av_fifo_reset(AVFifoBuffer *f)
{
    f->wptr = f->rptr = f->buffer;
    f->wndx = f->rndx = 0;
}
static int av_fifo_size(AVFifoBuffer *f)
{
    return (uint32_t)(f->wndx - f->rndx);
}

static int av_fifo_space(AVFifoBuffer *f)
{
    return f->end - f->buffer - av_fifo_size(f);
}
static int av_fifo_realloc2(AVFifoBuffer *f, unsigned int new_size) {
    unsigned int old_size= f->end - f->buffer;

    if(old_size < new_size){
        int len= av_fifo_size(f);
        AVFifoBuffer *f2= av_fifo_alloc(new_size);

        if (!f2)
            return -1;
        av_fifo_generic_read(f, f2->buffer, len, NULL);
        f2->wptr += len;
        f2->wndx += len;
        av_free(f->buffer);
        *f= *f2;
        av_free(f2);
    }
    return 0;
}
static int av_fifo_generic_write(AVFifoBuffer *f, void *src, int size, int (*func)(void*, void*, int))
{
    int total = size;
    do {
        int len = FFMIN(f->end - f->wptr, size);
        if(func) {
            if(func(src, f->wptr, len) <= 0)
                break;
        } else {
            veejay_memcpy(f->wptr, src, len);
            src = (uint8_t*)src + len;
        }
// Write memory barrier needed for SMP here in theory
        f->wptr += len;
        if (f->wptr >= f->end)
            f->wptr = f->buffer;
        f->wndx += len;
        size -= len;
    } while (size > 0);
    return total - size;
}
static int av_fifo_generic_read(AVFifoBuffer *f, void *dest, int buf_size, void (*func)(void*, void*, int))
{
// Read memory barrier needed for SMP here in theory
    do {
        int len = FFMIN(f->end - f->rptr, buf_size);
        if(func) func(dest, f->rptr, len);
        else{
            veejay_memcpy(dest, f->rptr, len);
            dest = (uint8_t*)dest + len;
        }
// memory barrier needed for SMP here in theory
        av_fifo_drain(f, len);
        buf_size -= len;
    } while (buf_size > 0);
    return 0;
}

/** Discard data from the FIFO. */
static void av_fifo_drain(AVFifoBuffer *f, int size)
{
    f->rptr += size;
    if (f->rptr >= f->end)
        f->rptr -= f->end - f->buffer;
    f->rndx += size;
}


static int write_buffer(unsigned char* data, int len) {
  int free = av_fifo_space(buffer);
  if (len > free) len = free;
  int res = av_fifo_generic_write(buffer,data,len,NULL );
  return res;
}

static void deinterleave(void *info, void *src, int len) {
  struct deinterleave *di = info;
  float *s = src;
  int i;
  len /= sizeof(float);
  for (i = 0; i < len; i++) {
    di->bufs[di->cur_buf++][di->pos] = s[i];
    if (di->cur_buf >= di->num_bufs) {
      di->cur_buf = 0;
      di->pos++;
    }
  }
}

/**
 * \brief read data from buffer and splitting it into channels
 * \param bufs num_bufs float buffers, each will contain the data of one channel
 * \param cnt number of samples to read per channel
 * \param num_bufs number of channels to split the data into
 * \return number of samples read per channel, equals cnt unless there was too
 *         little data in the buffer
 *
 * Assumes the data in the buffer is of type float, the number of bytes
 * read is res * num_bufs * sizeof(float), where res is the return value.
 * If there is not enough data in the buffer remaining parts will be filled
 * with silence.
 */
static int read_buffer(float **bufs, int cnt, int num_bufs) {
  struct deinterleave di = {bufs, num_bufs, 0, 0};
  int buffered = av_fifo_size(buffer);
  if (cnt * sizeof(float) * num_bufs > buffered) {
    silence(bufs, cnt, num_bufs);
    cnt = buffered / sizeof(float) / num_bufs;
  }
  av_fifo_generic_read(buffer, &di, cnt * num_bufs * sizeof(float), deinterleave);
  return cnt;
}

static void silence(float **bufs, int cnt, int num_bufs) {
  int i;
  for (i = 0; i < num_bufs; i++)
    veejay_memset(bufs[i], 0, cnt * sizeof(float));
}
/**
 * \brief JACK Callback function
 * \param nframes number of frames to fill into buffers
 * \param arg unused
 * \return currently always 0
 *
 * Write silence into buffers if paused or an underrun occured
 */
static int outputaudio(jack_nframes_t nframes, void *arg) {
  float *bufs[MAX_CHANS];
  int i;

  gettimeofday(&callbackTime,NULL);

  veejay_msg(0, "write to jack at time: %ld,%ld",
		  	callbackTime.tv_sec,callbackTime.tv_usec);

  for (i = 0; i < num_ports; i++)
    bufs[i] = jack_port_get_buffer(ports[i], nframes);

  if (paused || underrun)
    silence(bufs, nframes, num_ports);
  else
    if (read_buffer(bufs, nframes, num_ports) < nframes)
      underrun = 1;

  if (estimate) {
    float now = (float)GetTimer() / 1000000.0;
    float diff = callback_time + callback_interval - now;
    if ((diff > -0.002) && (diff < 0.002))
      callback_time += callback_interval;
    else
      callback_time = now;
    callback_interval = (float)nframes / (float)audio_rate;
  }

  bytes_buffered += (nframes * 4);
  return 0;
}

int audio_init(int rate, int channels, char *port_name, char *client_name) {
  const char **matching_ports = NULL;
  int autostart = 0;
  estimate  = 0;
//  jack_options_t open_options = JackUseExactName;
  jack_options_t open_options = JackNullOption;
  jack_status_t status;
  int port_flags = JackPortIsInput;
  int i;

//@ server_name specificed ? open_options |= JackServerName

  if (channels > MAX_CHANS) {
	veejay_msg(0, "[JACK] Invalid number of channels: %i\n", channels );
    	goto err_out;
  }

  if (!autostart)
    open_options |= JackNoStartServer;
  
  client = jack_client_open(client_name, open_options,&status, NULL);
  if (!client) {
    veejay_msg(0, "[JACK] cannot open server status = 0x%2.0x", status);
    goto err_out;
  }
  if( status & JackServerStarted) {
	  veejay_msg(VEEJAY_MSG_INFO, "Jack Server started");
	 }
  if( status & JackNameNotUnique) {
	  veejay_msg(VEEJAY_MSG_INFO, "Client name not unique, could use ",
			  	jack_get_client_name(client));
	 }

  buffer = av_fifo_alloc(BUFFSIZE);
  jack_set_process_callback(client, outputaudio, 0);

  // list matching ports
  if (!port_name)
    port_flags |= JackPortIsPhysical;

  veejay_msg(VEEJAY_MSG_DEBUG, "Jack client: %s, port %s", client_name, port_name );

  matching_ports = jack_get_ports(client, port_name, NULL, port_flags);
  if (!matching_ports || !matching_ports[0]) {
    veejay_msg(0, "[JACK] no physical ports available\n");
    goto err_out;
  }

  i = 1;
  while (matching_ports[i]) i++;
  if (channels > i) channels = i;
  num_ports = channels;

  // create out output ports
  for (i = 0; i < num_ports; i++) {
    char pname[30];
    snprintf(pname, 30, "output_%d", i);
    ports[i] = jack_port_register(client, pname, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (!ports[i]) {
      veejay_msg(0, "[JACK] not enough ports available\n");
      goto err_out;
    }
  }
  if (jack_activate(client)) {
    veejay_msg(0,"[JACK] activate failed\n");
    goto err_out;
  }
  for (i = 0; i < num_ports; i++) {
    if (jack_connect(client, jack_port_name(ports[i]), matching_ports[i])) {
      veejay_msg(0, "[JACK] connecting failed\n");
      goto err_out;
    }
  }
  rate = jack_get_sample_rate(client);
  latency = (float)(jack_port_get_total_latency(client, ports[0]) +
                         jack_get_buffer_size(client)) / (float)rate;
  callback_interval = 0;
/*
  ao_data.channels = channels;
  ao_data.samplerate = rate;
  ao_data.format = AF_FORMAT_FLOAT_NE;
  ao_data.bps = channels * rate * sizeof(float);
  ao_data.buffersize = CHUNK_SIZE * NUM_CHUNKS;
  ao_data.outburst = CHUNK_SIZE;
*/
  bps = channels * rate * sizeof(float);
  audio_rate = rate;
  veejay_msg(0, "Channels:%d, Rate:%d, BPS:%d, BufferSize: %d, OutBurst:%d",
		  channels,rate,channels*rate*sizeof(float), CHUNK_SIZE*NUM_CHUNKS, CHUNK_SIZE );

  free(matching_ports);
  //free(port_name);
  //free(client_name);
  return 1;

err_out:
  if( matching_ports )
	  free(matching_ports);
//  free(port_name);
//  free(client_name);
  if (client)
    jack_client_close(client);
  av_fifo_free(buffer);
  buffer = NULL;
  return 0;
}

/**
 * \brief stop playing and empty buffers (for seeking/pause)
 */
void audio_reset(void) {
  paused = 1;
  av_fifo_reset(buffer);
  paused = 0;
}

/**
 * \brief stop playing, keep buffers (for pause)
 */
void audio_pause(void) {
  paused = 1;
}


void	audio_continue(int speed)
{
	if( speed == 0 )
	 audio_pause;
	else
	 audio_resume;
}

/**
 * \brief resume playing, after audio_pause()
 */
void audio_resume(void) {
  paused = 0;
}

static int get_space(void) {
  return av_fifo_space(buffer);
}
#define AOPLAY_FINAL_CHUNK 1
/**
 * \brief write data into buffer and reset underrun flag
 */
int audio_play(void *data, int len, int flags) {
//  if (!(flags & AOPLAY_FINAL_CHUNK))
//    len -= len % CHUNK_SIZE; //@ OutBurst
  underrun = 0;
  return write_buffer(data, len);
}

static int usec_sleep(int usec_delay)
{
    struct timespec ts;
    ts.tv_sec  =  usec_delay / 1000000;
    ts.tv_nsec = (usec_delay % 1000000) * 1000;
    return clock_nanosleep(CLOCK_REALTIME,0,&ts, NULL);
}

static unsigned int GetTimer(void){
  struct timeval tv;
  gettimeofday(&tv,NULL);
  return tv.tv_sec * 1000000 + tv.tv_usec;
}  

float audio_get_delay(struct timeval bs) {
  if( buffer == NULL )
	  return 0.0f;
  int buffered = av_fifo_size(buffer); // could be less
  float in_jack = latency;
  float b=0.0f,elapsed=0.0;
  if (estimate && callback_interval > 0) { 
   unsigned int b1 = bs.tv_sec * 1000000 + bs.tv_usec;
    b       = (float) b1/1000000.0;
    elapsed = (float)GetTimer() / 1000000.0 ;
    elapsed -= callback_time;
    in_jack += callback_interval - elapsed;
    if (in_jack < 0) in_jack = 0;
  }
  return (float)buffered / (float)bps + in_jack;
}

int	audio_get_buffered_bytes(long *sec, long *usec)
{
	if( buffer == NULL ) {
		veejay_msg(0, "Audio buffer not ready.");
		return 0;
	}
	int buffered = av_fifo_size(buffer);
	*sec = callbackTime.tv_sec;
	*usec = callbackTime.tv_usec;
	veejay_msg(0, "%s: %d %ld, %ld",__FUNCTION__,buffered, callbackTime.tv_sec,callbackTime.tv_usec );
	return buffered;
}
#endif
