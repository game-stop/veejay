/*
 * Copyright 2003-2006 Chris Morgan <cmorgan@alum.wpi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
  calls functions in veejay
  replaced gettimeofday for clock_gettime
 */
#include <config.h>
#ifdef HAVE_JACK
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>
#include <inttypes.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <pthread.h>
#include <sys/time.h>
#include <libvjmsg/vj-msg.h>
#include "bio2jack.h"

/* set to 1 to enable debug messages */
#define DEBUG_OUTPUT            1

#define DEFAULT_RB_SIZE         4096

#define OUTFILE stderr

#define DEBUG(format,args...) veejay_msg(4, "bio2jack: "format,##args);
#define WARN(format,args...) veejay_msg(1, "bio2jack: "format,##args);	

#define ERR(format,args...) veejay_msg(0, "bio2jack: "format,##args);	
#define min(a,b)   (((a) < (b)) ? (a) : (b))
#define max(a,b)   (((a) < (b)) ? (b) : (a))

#define MAX_OUTPUT_PORTS  2
#define MAX_INPUT_PORTS   0

#define DEFAULT_VOLUME 100

typedef struct jack_driver_s
{
  bool allocated;                       /* whether or not this device has been allocated */

  int deviceID;                         /* id of this device */
  int clientCtr;                        /* to prevent overlapping client ids */
  long jack_sample_rate;                /* jack samples(frames) per second */

  long client_sample_rate;              /* client samples(frames) per second */
  double output_sample_rate_ratio;      /* ratio between jack's output rate & ours */
  double input_sample_rate_ratio;       /* ratio between our input rate & jack's */

  unsigned long num_input_channels;     /* number of input channels(1 is mono, 2 stereo etc..) */
  unsigned long num_output_channels;    /* number of output channels(1 is mono, 2 stereo etc..) */

  unsigned long bits_per_channel;       /* number of bits per channel (only 8 & 16 are currently supported) */

  unsigned long bytes_per_output_frame; /* (num_output_channels * bits_per_channel) / 8 */
  unsigned long bytes_per_input_frame;  /* (num_input_channels * bits_per_channel) / 8 */

  unsigned long bytes_per_jack_output_frame;    /* (num_output_channels * bits_per_channel) / 8 */
  unsigned long bytes_per_jack_input_frame;     /* (num_input_channels * bits_per_channel) / 8 */

//  long ticks;
//  long chunk_size;

  unsigned long latencyMS;      /* latency in ms between writing and actual audio output of the written data */

  long clientBytesInJack;       /* number of INPUT bytes(from the client of bio2jack) we wrote to jack(not necessary the number of bytes we wrote to jack) */
  long jack_buffer_size;        /* size of the buffer jack will pass in to the process callback */

  unsigned long callback_buffer1_size;  /* number of bytes in the buffer allocated for processing data in JACK_Callback */
  char *callback_buffer1;
  unsigned long callback_buffer2_size;  /* number of bytes in the buffer allocated for processing data in JACK_Callback */
  char *callback_buffer2;

  unsigned long rw_buffer1_size;        /* number of bytes in the buffer allocated for processing data in JACK_(Read|Write) */
  char *rw_buffer1;

  struct timespec previousTime;  /* time of last JACK_Callback() write to jack, allows for MS accurate bytes played  */

  unsigned long written_client_bytes;   /* input bytes we wrote to jack, not necessarily actual bytes we wrote to jack due to channel and other conversion */
  unsigned long played_client_bytes;    /* input bytes that jack has played */

  unsigned long client_bytes;   /* total bytes written by the client of bio2jack via JACK_Write() */

  jack_port_t *output_port[MAX_OUTPUT_PORTS];   /* output ports */
  jack_port_t *input_port[MAX_OUTPUT_PORTS];    /* input ports */

  jack_client_t *client;        /* pointer to jack client */

  char **jack_port_name;        /* user given strings for the port names, can be NULL */
  unsigned int jack_port_name_count;    /* the number of port names given */

  unsigned long jack_output_port_flags; /* flags to be passed to jack when opening the output ports */
  unsigned long jack_input_port_flags;  /* flags to be passed to jack when opening the output ports */

  jack_ringbuffer_t *pPlayPtr;  /* the playback ringbuffer */
  jack_ringbuffer_t *pRecPtr;   /* the recording ringbuffer */
/*
  SRC_STATE *output_src;        
  SRC_STATE *input_src;    
*/

  enum status_enum state;       /* one of PLAYING, PAUSED, STOPPED, CLOSED, RESET etc */

  unsigned int volume[MAX_OUTPUT_PORTS];        /* percentage of sample value to preserve, 100 would be no attenuation */
  enum JACK_VOLUME_TYPE volumeEffectType;       /* linear or dbAttenuation, if dbAttenuation volume is the number of dBs of
                                                   attenuation to apply, 0 volume being no attenuation, full volume */

  long position_byte_offset;    /* an offset that we will apply to returned position queries to achieve */
                                /* the position that the user of the driver desires set */

  bool in_use;                  /* true if this device is currently in use */

  pthread_mutex_t mutex;        /* mutex to lock this specific device */

  /* variables used for trying to restart the connection to jack */
  bool jackd_died;              /* true if jackd has died and we should try to restart it */
  struct timespec last_reconnect_attempt;
} jack_driver_t;


static char *client_name = NULL;       /* the name bio2jack will use when creating a new
                                   jack client. client_name_%deviceID% will be used */

/*
  Which SRC converter function we should use when doing sample rate conversion.
  Default to the fastest of the 'good quality' set.
 */
static bool init_done = 0;      /* just to prevent clients from calling JACK_Init twice, that would be very bad */

static enum JACK_PORT_CONNECTION_MODE port_connection_mode = CONNECT_ALL;

/* enable/disable code that allows us to close a device without actually closing the jack device */
/* this works around the issue where jack doesn't always close devices by the time the close function call returns */
#define JACK_CLOSE_HACK 1

typedef jack_default_audio_sample_t sample_t;
typedef jack_nframes_t nframes_t;

/* allocate devices for output */
/* if you increase this past 10, you might want to update 'out_client_name = ... ' in JACK_OpenDevice */
#define MAX_OUTDEVICES 4
static jack_driver_t outDev[MAX_OUTDEVICES];

static pthread_mutex_t device_mutex = PTHREAD_MUTEX_INITIALIZER;        /* this is to lock the entire outDev array
                                                                           to make managing it in a threaded
                                                                           environment sane */

#if JACK_CLOSE_HACK
static void JACK_CloseDevice(jack_driver_t * drv, bool close_client);
#else
static void JACK_CloseDevice(jack_driver_t * drv);
#endif


/* Prototypes */
static int JACK_OpenDevice(jack_driver_t * drv);
static unsigned long JACK_GetBytesFreeSpaceFromDriver(jack_driver_t * drv);
static void JACK_ResetFromDriver(jack_driver_t * drv);
static long JACK_GetPositionFromDriver(jack_driver_t * drv,
                                       enum pos_enum position, int type);
static void JACK_CleanupDriver(jack_driver_t * drv);


/* Return the difference between two timeval structures in terms of milliseconds */

long
TimeValDifference1(struct timespec *start, struct timespec *end)
{
  double long ms;               /* milliseconds value */

  ms = end->tv_sec - start->tv_sec;     /* compute seconds difference */
  ms *= (double) 1000;          /* convert to milliseconds */

 	//  ms += (double) (end->tv_usec - start->tv_usec) / (double) 1000;       /* add on microseconds difference */

  return (long) ms;
}

long
TimeValDifference( struct timespec *start, struct timespec *end)
{
	return (( end->tv_sec * 1000000000) + end->tv_nsec ) -
	       (( start->tv_sec * 1000000000) + start->tv_nsec );
}

/* get a device and lock the devices mutex */
/* */
/* also attempt to reconnect to jack since this function is called from */
/* most other bio2jack functions it provides a good point to attempt reconnection */
/* */
/* Ok, I know this looks complicated and it kind of is. The point is that when you're
   trying to trace mutexes it's more important to know *who* called us than just that
   we were called.  This uses from pre-processor trickery so that the fprintf is actually
   placed in the function making the getDriver call.  Thus, the __FUNCTION__ and __LINE__
   macros will actually reference our caller, rather than getDriver.  The reason the 
   fprintf call is passes as a parameter is because this macro has to still return a
   jack_driver_t* and we want to log both before *and* after the getDriver call for
   easier detection of blocked calls.
 */
jack_driver_t *
getDriver(int deviceID)
{
  jack_driver_t *drv = &outDev[deviceID];

  /* should we try to restart the jack server? */
  if(drv->jackd_died && drv->client == 0)
  {
	struct timespec now;
	clock_gettime( CLOCK_REALTIME, &now);

    /* wait 250ms before trying again */
    if(TimeValDifference(&drv->last_reconnect_attempt, &now) >= 250)
    {
      JACK_OpenDevice(drv);
      drv->last_reconnect_attempt = now;
      veejay_msg(VEEJAY_MSG_WARNING, "Last connection attempt to Jack!");
    }
  }

  return drv;
}


/* Return a string corresponding to the input state */
char *
DEBUGSTATE(enum status_enum state)
{
  if(state == PLAYING)
    return "PLAYING";
  else if(state == PAUSED)
    return "PAUSED";
  else if(state == STOPPED)
    return "STOPPED";
  else if(state == CLOSED)
    return "CLOSED";
  else if(state == RESET)
    return "RESET";
  else
    return "unknown state";
}


#define SAMPLE_MAX_16BIT  32767.0f
#define SAMPLE_MAX_8BIT   255.0f

/* floating point volume routine */
/* volume should be a value between 0.0 and 1.0 */
static void
float_volume_effect(sample_t * buf, unsigned long nsamples, float volume,
                    int skip)
{
  if(volume < 0)
    volume = 0;
  if(volume > 1.0)
    volume = 1.0;

  while(nsamples--)
  {
    *buf = (*buf) * volume;
    buf += skip;
  }
}

/* place one channel into a multi-channel stream */
static inline void
mux(sample_t * dst, sample_t * src, unsigned long nsamples,
    unsigned long dst_skip)
{
  /* ALERT: signed sign-extension portability !!! */
  while(nsamples--)
  {
    *dst = *src;
    dst += dst_skip;
    src++;
  }
}

/* pull one channel out of a multi-channel stream */
static void
demux(sample_t * dst, sample_t * src, unsigned long nsamples,
      unsigned long src_skip)
{
  /* ALERT: signed sign-extension portability !!! */
  while(nsamples--)
  {
    *dst = *src;
    dst++;
    src += src_skip;
  }
}

/* convert from 16 bit to floating point */
static inline void
sample_move_short_float(sample_t * dst, short *src, unsigned long nsamples)
{
  /* ALERT: signed sign-extension portability !!! */
  unsigned long i;
  for(i = 0; i < nsamples; i++)
    dst[i] = (sample_t) (src[i]) / SAMPLE_MAX_16BIT;
}

/* convert from floating point to 16 bit */
static inline void
sample_move_float_short(short *dst, sample_t * src, unsigned long nsamples)
{
  /* ALERT: signed sign-extension portability !!! */
  unsigned long i;
  for(i = 0; i < nsamples; i++)
    dst[i] = (short) ((src[i]) * SAMPLE_MAX_16BIT);
}

/* convert from 8 bit to floating point */
static inline void
sample_move_char_float(sample_t * dst, unsigned char *src, unsigned long nsamples)
{
  /* ALERT: signed sign-extension portability !!! */
  unsigned long i;
  for(i = 0; i < nsamples; i++)
    dst[i] = (sample_t) (src[i]) / SAMPLE_MAX_8BIT;
}

/* convert from floating point to 8 bit */
static inline void
sample_move_float_char(unsigned char *dst, sample_t * src, unsigned long nsamples)
{
  /* ALERT: signed sign-extension portability !!! */
  unsigned long i;
  for(i = 0; i < nsamples; i++)
    dst[i] = (char) ((src[i]) * SAMPLE_MAX_8BIT);
}

/* fill dst buffer with nsamples worth of silence */
static inline void
sample_silence_float(sample_t * dst, unsigned long nsamples)
{
  /* ALERT: signed sign-extension portability !!! */
  while(nsamples--)
  {
    *dst = 0;
    dst++;
  }
}

static inline bool
ensure_buffer_size(char **buffer, unsigned long *cur_size,
                   unsigned long needed_size)
{
  if(*cur_size >= needed_size)
    return TRUE;
  char *tmp = realloc(*buffer, needed_size);
  if(tmp)
  {
    *cur_size = needed_size;
    *buffer = tmp;
    return TRUE;
  }
  return FALSE;
}

static int
JACK_xrun_callback(void *arg)
{
  jack_driver_t *drv = (jack_driver_t *) arg;
  veejay_msg(1, "xrun detected. You are doing too much!");

  long wcb = drv->written_client_bytes;
  long pcb = drv->played_client_bytes;     /* number of the clients bytes that jack has played */


  JACK_SetState( drv->deviceID, RESET );
  
  drv->written_client_bytes = wcb;
  drv->played_client_bytes = pcb;

  return 0;
}

/******************************************************************
 *    JACK_callback
 *
 * every time the jack server wants something from us it calls this
 * function, so we either deliver it some sound to play or deliver it nothing
 * to play
 */

/*
 * jack ringbuffer is supposed to be free of threading problems...
 * The key attribute of a ringbuffer is that it can be safely accessed by two threads simultaneously 
 * -- one reading from the buffer and t the other writing to it 
 * -- without using any synchronization or mutual exclusion primitives. For this to work correctly, 
 *there can only be a single reader and a single writer thread. Their identities cannot be interchanged. 
 *
 */
static int
JACK_callback(nframes_t nframes, void *arg)
{
  jack_driver_t *drv = (jack_driver_t *) arg;
  struct timespec tmp_tp;
  unsigned int i;

  clock_gettime(CLOCK_REALTIME, &tmp_tp );

  __sync_lock_test_and_set( &(drv->previousTime.tv_sec), tmp_tp.tv_sec );
  __sync_lock_test_and_set( &(drv->previousTime.tv_nsec), tmp_tp.tv_nsec );

  if(!drv->client)
    ERR("client is closed, this is weird...");

  sample_t *out_buffer[MAX_OUTPUT_PORTS];
  /* retrieve the buffers for the output ports */
  for(i = 0; i < drv->num_output_channels; i++)
    out_buffer[i] = (sample_t *) jack_port_get_buffer(drv->output_port[i], nframes);

  sample_t *in_buffer[MAX_INPUT_PORTS];
  /* retrieve the buffers for the input ports */
  for(i = 0; i < drv->num_input_channels; i++)
    in_buffer[i] = (sample_t *) jack_port_get_buffer(drv->input_port[i], nframes);

  /* handle playing state */
//  if(drv->state == PLAYING)
//  {
    /* handle playback data, if any */
    if(drv->num_output_channels > 0)
    {
      unsigned long jackFramesAvailable = nframes;      /* frames we have left to write to jack */
      unsigned long numFramesToWrite = nframes;   /* num frames we are writing */
      size_t inputBytesAvailable = jack_ringbuffer_read_space(drv->pPlayPtr);
      unsigned long inputFramesAvailable;       /* frames we have available */

      inputFramesAvailable = inputBytesAvailable / drv->bytes_per_jack_output_frame;
      size_t jackBytesAvailable = jackFramesAvailable * drv->bytes_per_jack_output_frame;

      long read = 0;

#if JACK_CLOSE_HACK
      if(drv->in_use == FALSE)
      {
        /* output silence if nothing is being outputted */
        for(i = 0; i < drv->num_output_channels; i++)
          sample_silence_float(out_buffer[i], nframes);
        return -1;
      }
#endif

      /* make sure our buffer is large enough for the data we are writing */
      /* ie. callback_buffer2_size < (bytes we already wrote + bytes we are going to write in this loop) */
      if(!ensure_buffer_size
         (&drv->callback_buffer2, &drv->callback_buffer2_size,
          jackBytesAvailable))
      {
        return -1;
      }

        /* read as much data from the buffer as is available */
        if(jackFramesAvailable && inputBytesAvailable > 0)
        {
          /* write as many bytes as we have space remaining, or as much as we have data to write */
          numFramesToWrite = min(jackFramesAvailable, inputFramesAvailable);
          jack_ringbuffer_read(drv->pPlayPtr, drv->callback_buffer2,
                               jackBytesAvailable);
          /* add on what we wrote */
          read = numFramesToWrite * drv->bytes_per_output_frame;
          jackFramesAvailable -= numFramesToWrite;      /* take away what was written */
        }

//      drv->ticks ++;

	  __sync_add_and_fetch( &(drv->written_client_bytes), read );
     // drv->written_client_bytes += read;
     // drv->played_client_bytes += drv->clientBytesInJack;       /* move forward by the previous bytes we wrote since those must have finished by now */
      drv->clientBytesInJack = read;    /* record the input bytes we wrote to jack */
	
      /* see if we still have jackBytesLeft here, if we do that means that we
         ran out of wave data to play and had a buffer underrun, fill in
         the rest of the space with zero bytes so at least there is silence */
      if(jackFramesAvailable)
      {
        DEBUG("buffer underrun of %ld frames", jackFramesAvailable);		
  	    for(i = 0; i < drv->num_output_channels; i++)
          sample_silence_float(out_buffer[i] +
                               (nframes - jackFramesAvailable),
                               jackFramesAvailable);
      }

      /* apply volume and demux */
      if(drv->output_sample_rate_ratio == 1.0)
      {
          for(i = 0; i < drv->num_output_channels; i++)
          {
		if( drv->volume[i] == DEFAULT_VOLUME )
			continue;

              if(drv->volumeEffectType == dbAttenuation)
              {
                  // assume the volume setting is dB of attenuation, a volume of 0 
                  // is 0dB attenuation 
                  float volume = powf(10.0, -((float) drv->volume[i]) / 20.0);
                  float_volume_effect((sample_t *) drv->callback_buffer2 + i,
                                      (nframes - jackFramesAvailable), volume, drv->num_output_channels);
              } else
              {
                  float_volume_effect((sample_t *) drv->callback_buffer2 + i, (nframes - jackFramesAvailable),
                                      ((float) drv->volume[i] / 100.0),
                                      drv->num_output_channels);
              }
          }
	} 
	if( !(drv->output_sample_rate_ratio != 1.0))
	{
          /* demux the stream: we skip over the number of samples we have output channels as the channel data */
          /* is encoded like chan1,chan2,chan3,chan1,chan2,chan3... */
          for(i = 0; i < drv->num_output_channels; i++)
          {
              demux(out_buffer[i],
                    (sample_t *) drv->callback_buffer2 + i,
                    (nframes - jackFramesAvailable), drv->num_output_channels);
          }

      }
    }

    /* handle record data, if any */
    if(drv->num_input_channels > 0)
    {
      long jack_bytes = nframes * drv->bytes_per_jack_input_frame;      /* how many bytes jack is feeding us */

      if(!ensure_buffer_size(&drv->callback_buffer1, &drv->callback_buffer1_size, jack_bytes))
      {
        ERR("allocated %lu bytes, need %lu bytes",
            drv->callback_buffer1_size, jack_bytes);
        return -1;
      }

      /* mux the invividual channels into one stream */
      for(i = 0; i < drv->num_input_channels; i++)
      {
        mux((sample_t *) drv->callback_buffer1 + i, in_buffer[i],
            nframes, drv->num_input_channels);
      }

   /*     long write_space = jack_ringbuffer_write_space(drv->pRecPtr);
        if(write_space < jack_bytes)
        {
            write_space = jack_ringbuffer_write_space(drv->pRecPtr);
            if(write_space < jack_bytes)
            {
             ERR("buffer overrun of %ld bytes", jack_bytes - write_space);
             jack_ringbuffer_read_advance(drv->pRecPtr, jack_bytes - write_space);
            }
        }*/

        jack_ringbuffer_write(drv->pRecPtr, drv->callback_buffer1,
                              jack_bytes);
    }
 
  return 0;
}


/******************************************************************
 *             JACK_bufsize
 *
 *             Called whenever the jack server changes the the max number
 *             of frames passed to JACK_callback
 */
static int
JACK_bufsize(nframes_t nframes, void *arg)
{
  jack_driver_t *drv = (jack_driver_t *) arg;

  drv->jack_buffer_size = nframes;

  return 0;
}

/******************************************************************
 *		JACK_srate
 */
int
JACK_srate(nframes_t nframes, void *arg)
{
  jack_driver_t *drv = (jack_driver_t *) arg;

  drv->jack_sample_rate = (long) nframes;
  drv->output_sample_rate_ratio = 1.0;
  drv->input_sample_rate_ratio = 1.0;
  return 0;
}


/******************************************************************
 *		JACK_shutdown
 *
 * if this is called then jack shut down... handle this appropriately */
void
JACK_shutdown(void *arg)
{
  jack_driver_t *drv = (jack_driver_t *) arg;

  getDriver(drv->deviceID);

  drv->client = 0;              /* reset client */
  drv->jackd_died = TRUE;

  DEBUG("jack shutdown, setting client to 0 and jackd_died to true, closing device");

#if JACK_CLOSE_HACK
  JACK_CloseDevice(drv, TRUE);
#else
  JACK_CloseDevice(drv);
#endif

  DEBUG("trying to reconnect right now");
  
  /* lets see if we can't reestablish the connection */
  
  //@ doesnt work anymore.
  /*if(JACK_OpenDevice(drv) != ERR_SUCCESS)
  {
    ERR("unable to reconnect with jack");
  }*/
  
  ERR("unable to reconnect with jack");

  veejay_msg(VEEJAY_MSG_ERROR, "Cannot recover from this error! You will probably need to restart for Audio playback.");

}


/******************************************************************
 *		JACK_Error
 *
 * Callback for jack errors
 */
static void
JACK_Error(const char *desc)
{
  ERR("%s", desc);
}


/******************************************************************
 *		JACK_OpenDevice
 *
 *  RETURNS: ERR_SUCCESS upon success
 */
static int
JACK_OpenDevice(jack_driver_t * drv)
{
  const char **ports;
  char *our_client_name = 0;
  unsigned int i;
  int failed = 0;

  DEBUG("creating jack client and setting up callbacks");

#if JACK_CLOSE_HACK
  /* see if this device is already open */
  if(drv->client)
  {
    /* if this device is already in use then it is bad for us to be in here */
    if(drv->in_use)
      return ERR_OPENING_JACK;

    DEBUG("using existing client");
    drv->in_use = TRUE;
    return ERR_SUCCESS;
  }
#endif

  /* set up an error handler */
  jack_set_error_function(JACK_Error);


  /* build the client name */
  our_client_name = (char *) malloc(snprintf
                                    (our_client_name, 0, "%s_%d_%d%02d", client_name, getpid(),
                                     drv->deviceID, drv->clientCtr + 1) + 1);
  sprintf(our_client_name, "%s_%d_%d%02d", client_name, getpid(),
          drv->deviceID, drv->clientCtr++);

  /* try to become a client of the JACK server */
  DEBUG("client name '%s'", our_client_name); 
#ifndef HAVE_JACK2
    if((drv->client = jack_client_new(our_client_name)) == 0)
#else
  if( (drv->client = jack_client_open( our_client_name, JackNullOption | JackNoStartServer, NULL ) ) == 0 )
#endif
  {
    /* try once more */
    DEBUG("trying once more to jack_client_new");
#ifndef HAVE_JACK2
	if((drv->client = jack_client_new(our_client_name)) == 0)
#else
	if( (drv->client = jack_client_open( our_client_name, JackNullOption, NULL )) == 0)
#endif
	{
      ERR("jack server not running?");
      free(our_client_name);
      return ERR_OPENING_JACK;
    }
  }

  free(our_client_name);

  drv->client_sample_rate = jack_get_sample_rate(drv->client);
 
  /* JACK server to call `JACK_callback()' whenever
     there is work to be done. */
  jack_set_process_callback(drv->client, JACK_callback, drv);

  /* setup a buffer size callback */
  jack_set_buffer_size_callback(drv->client, JACK_bufsize, drv);

  /* tell the JACK server to call `srate()' whenever
     the sample rate of the system changes. */
  jack_set_sample_rate_callback(drv->client, JACK_srate, drv);

  /* set xrun callback */
  jack_set_xrun_callback(drv->client, JACK_xrun_callback, drv);

  /* tell the JACK server to call `jack_shutdown()' if
     it ever shuts down, either entirely, or if it
     just decides to stop calling us. */
  jack_on_shutdown(drv->client, JACK_shutdown, drv);

  /* display the current sample rate. once the client is activated
     (see below), you should rely on your own sample rate
     callback (see above) for this value. */
  drv->jack_sample_rate = jack_get_sample_rate(drv->client);
  drv->output_sample_rate_ratio = (double) drv->jack_sample_rate / (double) drv->client_sample_rate;
  drv->input_sample_rate_ratio = (double) drv->client_sample_rate / (double) drv->jack_sample_rate;
  DEBUG("client sample rate: %lu, jack sample rate: %lu, output ratio = %f, input ratio = %f",
        drv->client_sample_rate, drv->jack_sample_rate,
        drv->output_sample_rate_ratio, drv->input_sample_rate_ratio);

  drv->jack_buffer_size = jack_get_buffer_size(drv->client);

  /* create the output ports */
  for(i = 0; i < drv->num_output_channels; i++)
  {
    char portname[16];
    sprintf(portname, "out_%d", i);
    DEBUG("output port %d is named '%s'", i, portname);
    /* NOTE: Yes, this is supposed to be JackPortIsOutput since this is an output */
    /* port FROM bio2jack */
    drv->output_port[i] = jack_port_register(drv->client, portname,
                                             JACK_DEFAULT_AUDIO_TYPE,
                                             JackPortIsOutput, 0);
  }

  /* create the input ports */
  for(i = 0; i < drv->num_input_channels; i++)
  {
    char portname[16];
    sprintf(portname, "in_%d", i);
    DEBUG("intput port %d is named '%s'", i, portname);
    /* NOTE: Yes, this is supposed to be JackPortIsInput since this is an input */
    /* port TO bio2jack */
    drv->input_port[i] = jack_port_register(drv->client, portname,
                                            JACK_DEFAULT_AUDIO_TYPE,
                                            JackPortIsInput, 0);
  }

#if JACK_CLOSE_HACK
  drv->in_use = TRUE;
#endif

  /* tell the JACK server that we are ready to roll */
  if(jack_activate(drv->client))
  {
    ERR("cannot activate client");
    return ERR_OPENING_JACK;
  }

  /* if we have output channels and the port connection mode isn't CONNECT_NONE */
  /* then we should connect up some ports */
  if((drv->num_output_channels > 0) && (port_connection_mode != CONNECT_NONE))
  {
    /* determine how we are to acquire output port names */
    if((drv->jack_port_name_count == 0) || (drv->jack_port_name_count == 1))
    {
      if(drv->jack_port_name_count == 0)
      {
        ports = jack_get_ports(drv->client, NULL, NULL,
                               drv->jack_output_port_flags);
      }
      else
      {
        ports = jack_get_ports(drv->client, drv->jack_port_name[0], NULL,
                               drv->jack_output_port_flags);
      }

      /* display a trace of the output ports we found */
      unsigned int num_ports = 0;
      if(ports)
      {
        for(i = 0; ports[i]; i++)
        {
          num_ports++;
        }
      }

      /* ensure that we found enough ports */
      if(!ports || (i < drv->num_output_channels))
      {
        DEBUG("ERR: jack_get_ports() failed to find ports with jack port flags of 0x%lX'",
              drv->jack_output_port_flags);
#if JACK_CLOSE_HACK
        JACK_CloseDevice(drv, TRUE);
#else
        JACK_CloseDevice(drv);
#endif
        return ERR_PORT_NOT_FOUND;
      }

      /* connect a port for each output channel. Note: you can't do this before
         the client is activated (this may change in the future). */
      for(i = 0; i < drv->num_output_channels; i++)
      {
        DEBUG("jack_connect() to port %d('%p')", i, drv->output_port[i]);
        if(jack_connect(drv->client, jack_port_name(drv->output_port[i]), ports[i]))
        {
          ERR("cannot connect to output port %d('%s')", i, ports[i]);
          failed = 1;
        }
      }

      /* only if we are in CONNECT_ALL mode should we keep connecting ports up beyond */
      /* the minimum number of ports required for each output channel coming into bio2jack */
      if(port_connection_mode == CONNECT_ALL)
      {
          /* It's much cheaper and easier to let JACK do the processing required to
             connect 2 channels to 4 or 4 channels to 2 or any other combinations.
             This effectively eliminates the need for sample_move_d16_d16() */
          if(drv->num_output_channels < num_ports)
          {
              for(i = drv->num_output_channels; ports[i]; i++)
              {
                  int n = i % drv->num_output_channels;
                  DEBUG("jack_connect() to port %d('%p')", i, drv->output_port[n]);
                  if(jack_connect(drv->client, jack_port_name(drv->output_port[n]), ports[i]))
                  {
                      // non fatal
                      ERR("cannot connect to output port %d('%s')", n, ports[i]);
                  }
              }
          }
          else if(drv->num_output_channels > num_ports)
          {
              for(i = num_ports; i < drv->num_output_channels; i++)
              {
                  int n = i % num_ports;
                  DEBUG("jack_connect() to port %d('%p')", i, drv->output_port[n]);
                  if(jack_connect(drv->client, jack_port_name(drv->output_port[i]), ports[n]))
                  {
                      // non fatal
                      ERR("cannot connect to output port %d('%s')", i, ports[n]);
                  }
              }
          }
      }

      free(ports);              /* free the returned array of ports */
    }
    else
    {
      for(i = 0; i < drv->jack_port_name_count; i++)
      {
        ports = jack_get_ports(drv->client, drv->jack_port_name[i], NULL,
                               drv->jack_output_port_flags);

        if(!ports)
        {
          ERR("jack_get_ports() failed to find ports with jack port flags of 0x%lX'",
              drv->jack_output_port_flags);
          return ERR_PORT_NOT_FOUND;
        }

        /* connect the port */
        DEBUG("jack_connect() to port %d('%p')", i, drv->output_port[i]);
        if(jack_connect(drv->client, jack_port_name(drv->output_port[i]), ports[0]))
        {
          ERR("cannot connect to output port %d('%s')", 0, ports[0]);
          failed = 1;
        }
        free(ports);            /* free the returned array of ports */
      }
    }
  }                             /* if( drv->num_output_channels > 0 ) */


  if(drv->num_input_channels > 0)
  {
    /* determine how we are to acquire input port names */
    if((drv->jack_port_name_count == 0) || (drv->jack_port_name_count == 1))
    {
      if(drv->jack_port_name_count == 0)
      {
        ports = jack_get_ports(drv->client, NULL, NULL, drv->jack_input_port_flags);
      }
      else
      {
        ports = jack_get_ports(drv->client, drv->jack_port_name[0], NULL,
                               drv->jack_input_port_flags);
      }

      /* display a trace of the input ports we found */
      unsigned int num_ports = 0;
      if(ports)
      {
        for(i = 0; ports[i]; i++)
        {
          num_ports++;
        }
      }

      /* ensure that we found enough ports */
      if(!ports || (i < drv->num_input_channels))
      {
        DEBUG("ERR: jack_get_ports() failed to find ports with jack port flags of 0x%lX'",
              drv->jack_input_port_flags);
#if JACK_CLOSE_HACK
        JACK_CloseDevice(drv, TRUE);
#else
        JACK_CloseDevice(drv);
#endif
        return ERR_PORT_NOT_FOUND;
      }

      /* connect the ports. Note: you can't do this before
         the client is activated (this may change in the future). */
      for(i = 0; i < drv->num_input_channels; i++)
      {
        DEBUG("jack_connect() to input port %d('%p')", i, drv->input_port[i]);
        if(jack_connect(drv->client, ports[i], jack_port_name(drv->input_port[i])))
        {
          ERR("cannot connect to input port %d('%s')", i, ports[i]);
          failed = 1;
        }
      }

      /* It's much cheaper and easier to let JACK do the processing required to
         connect 2 channels to 4 or 4 channels to 2 or any other combinations.
         This effectively eliminates the need for sample_move_d16_d16() */
      if(drv->num_input_channels < num_ports)
      {
        for(i = drv->num_input_channels; ports[i]; i++)
        {
          int n = i % drv->num_input_channels;
          DEBUG("jack_connect() to input port %d('%p')", i, drv->input_port[n]);
          if(jack_connect(drv->client, ports[i], jack_port_name(drv->input_port[n])))
          {
            // non fatal
            ERR("cannot connect to input port %d('%s')", n, ports[i]);
          }
        }
      }
      else if(drv->num_input_channels > num_ports)
      {
        for(i = num_ports; i < drv->num_input_channels; i++)
        {
          int n = i % num_ports;
          DEBUG("jack_connect() to input port %d('%p')", i, drv->input_port[n]);
          if(jack_connect(drv->client, ports[n], jack_port_name(drv->input_port[i])))
          {
            // non fatal
            ERR("cannot connect to input port %d('%s')", i, ports[n]);
          }
        }
      }

      free(ports);              /* free the returned array of ports */
    }
    else
    {
      for(i = 0; i < drv->jack_port_name_count; i++)
      {
        ports = jack_get_ports(drv->client, drv->jack_port_name[i], NULL,
                               drv->jack_input_port_flags);

        if(!ports)
        {
          ERR("jack_get_ports() failed to find ports with jack port flags of 0x%lX'",
              drv->jack_input_port_flags);
          return ERR_PORT_NOT_FOUND;
        }

        /* connect the port */
        DEBUG("jack_connect() to input port %d('%p')", i, drv->input_port[i]);
        if(jack_connect(drv->client, jack_port_name(drv->input_port[i]), ports[0]))
        {
          ERR("cannot connect to input port %d('%s')", 0, ports[0]);
          failed = 1;
        }
        free(ports);            /* free the returned array of ports */
      }
    }
  }                             /* if( drv->num_input_channels > 0 ) */

  /* if something failed we need to shut the client down and return 0 */
  if(failed)
  {
    DEBUG("failed, closing and returning error");
#if JACK_CLOSE_HACK
    JACK_CloseDevice(drv, TRUE);
#else
    JACK_CloseDevice(drv);
#endif
    return ERR_OPENING_JACK;
  }

  drv->jackd_died = FALSE;      /* clear out this flag so we don't keep attempting to restart things */
  drv->state = PLAYING;         /* clients seem to behave much better with this on from the start, especially when recording */

  return ERR_SUCCESS;           /* return success */
}


/******************************************************************
 *		JACK_CloseDevice
 *
 *	Close the connection to the server cleanly.
 *  If close_client is TRUE we close the client for this device instead of
 *    just marking the device as in_use(JACK_CLOSE_HACK only)
 */
#if JACK_CLOSE_HACK
static void
JACK_CloseDevice(jack_driver_t * drv, bool close_client)
#else
static void
JACK_CloseDevice(jack_driver_t * drv)
#endif
{
  unsigned int i;

#if JACK_CLOSE_HACK
  if(close_client)
  {
#endif

    DEBUG("closing the jack client thread");
    if(drv->client)
    {
      int errorCode = jack_client_close(drv->client);
      if(errorCode)
        ERR("jack_client_close() failed returning an error code of %d", errorCode);
    }

    /* reset client */
    drv->client = 0;

    /* free up the port strings */
    if(drv->jack_port_name_count > 1)
    {
      for(i = 0; i < drv->jack_port_name_count; i++)
        free(drv->jack_port_name[i]);
      free(drv->jack_port_name);
    }
    JACK_CleanupDriver(drv);

    JACK_ResetFromDriver(drv);

#if JACK_CLOSE_HACK
  } else
  {
    drv->in_use = FALSE;

    if(!drv->client)
    {
      DEBUG("critical error, closing a device that has no client");
    }
  }
#endif
}

/**************************************/
/* External interface functions below */
/**************************************/

/* Clear out any buffered data, stop playing, zero out some variables */
static void
JACK_ResetFromDriver(jack_driver_t * drv)
{
  /* NOTE: we use the RESET state so we don't need to worry about clearing out */
  /* variables that the callback modifies while the callback is running */
  /* we set the state to RESET and the callback clears the variables out for us */
  drv->state = RESET;           /* tell the callback that we are to reset, the callback will transition this to STOPPED */
}

/* Clear out any buffered data, stop playing, zero out some variables */
void
JACK_Reset(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  JACK_ResetFromDriver(drv);
}


/*
 * open the audio device for writing to
 *
 * deviceID is set to the opened device
 * if client is non-zero and in_use is FALSE then just set in_use to TRU
 *
 * return value is zero upon success, non-zero upon failure 
 *
 * if ERR_RATE_MISMATCH (*rate) will be updated with the jack servers rate
 */
int
JACK_Open(int *deviceID, unsigned int bits_per_channel, unsigned long *rate,
          int channels)
{
  /* we call through to JACK_OpenEx(), but default the input channels to 0 for better backwards
     compatibility with clients written before recording was available */
  return JACK_OpenEx(deviceID, bits_per_channel,
                     rate,
                     0, channels,
                     NULL, 0, JackPortIsPhysical);
}

/*
 * see JACK_Open() for comments
 * NOTE: jack_port_name has three ways of being used:
 *       - NULL - finds all ports with the given flags
 *       - A single regex string used to retrieve all port names
 *       - A series of port names, one for each output channel
 *
 * we set *deviceID
 */
int
JACK_OpenEx(int *deviceID, unsigned int bits_per_channel,
            unsigned long *rate,
            unsigned int input_channels, unsigned int output_channels,
            const char **jack_port_name,
            unsigned int jack_port_name_count, unsigned long jack_port_flags)
{
  jack_driver_t *drv = 0;
  unsigned int i;
  int retval;

  if(input_channels < 1 && output_channels < 1)
  {
    ERR("no input OR output channels, nothing to do");
    return ERR_OPENING_JACK;
  }

  switch (bits_per_channel)
  {
  case 8:
  case 16:
    break;
  default:
    ERR("invalid bits_per_channel");
    return ERR_OPENING_JACK;
  }

  /* Lock the device_mutex and find one that's not allocated already.
     We'll keep this lock until we've either made use of it, or given up. */
  pthread_mutex_lock(&device_mutex);

  for(i = 0; i < MAX_OUTDEVICES; i++)
  {
    if(!outDev[i].allocated)
    {
      drv = &outDev[i];
      break;
    }
  }

  if(!drv)
  {
    ERR("no more devices available");
    return ERR_OPENING_JACK;
  }

  /* We found an unallocated device, now lock it for extra saftey */
  getDriver(drv->deviceID);

  DEBUG("bits_per_channel=%d rate=%ld, input_channels=%d, output_channels=%d",
     bits_per_channel, *rate, input_channels, output_channels);

  if(output_channels > MAX_OUTPUT_PORTS)
  {
    ERR("output_channels == %d, MAX_OUTPUT_PORTS == %d", output_channels,
        MAX_OUTPUT_PORTS);
    pthread_mutex_unlock(&device_mutex);
    return ERR_TOO_MANY_OUTPUT_CHANNELS;
  }

  if(input_channels > MAX_INPUT_PORTS)
  {
    ERR("input_channels == %d, MAX_INPUT_PORTS == %d", input_channels,
        MAX_INPUT_PORTS);
    pthread_mutex_unlock(&device_mutex);
    return ERR_TOO_MANY_INPUT_CHANNELS;
  }

  drv->jack_output_port_flags = jack_port_flags | JackPortIsInput;      /* port must be input(ie we can put data into it), so mask this in */
  drv->jack_input_port_flags = jack_port_flags | JackPortIsOutput;      /* port must be output(ie we can get data from it), so mask this in */

  /* check that we have the correct number of port names 
     FIXME?: not sure how we should handle output ports vs input ports....
   */
  if((jack_port_name_count > 1)
     && ((jack_port_name_count < output_channels)
         || (jack_port_name_count < input_channels)))
  {
    ERR("specified individual port names but not enough, gave %d names, need %d",
       jack_port_name_count, output_channels);
    pthread_mutex_unlock(&device_mutex);
    return ERR_PORT_NAME_OUTPUT_CHANNEL_MISMATCH;
  } else
  {
    /* copy this data into the device information */
    drv->jack_port_name_count = jack_port_name_count;

    if(drv->jack_port_name_count != 0)
    {
      drv->jack_port_name =
        (char **) malloc(sizeof(char *) * drv->jack_port_name_count);
      for(i = 0; i < drv->jack_port_name_count; i++)
      {
        drv->jack_port_name[i] = strdup(jack_port_name[i]);
        DEBUG("jack_port_name[%d] == '%s'", i, jack_port_name[i]);
      }
    } else
    {
      drv->jack_port_name = NULL;
    }
  }

  /* initialize some variables */
  drv->in_use = FALSE;

  JACK_ResetFromDriver(drv);    /* flushes all queued buffers, sets status to STOPPED and resets some variables */

  /* drv->jack_sample_rate is set by JACK_OpenDevice() */
    drv->client_sample_rate = *rate;
  drv->bits_per_channel = bits_per_channel;
  drv->num_input_channels = input_channels;
  drv->num_output_channels = output_channels;
  drv->bytes_per_input_frame = (drv->bits_per_channel * drv->num_input_channels) / 8;
  drv->bytes_per_output_frame = (drv->bits_per_channel * drv->num_output_channels) / 8;
  drv->bytes_per_jack_output_frame = sizeof(sample_t) * drv->num_output_channels;
  drv->bytes_per_jack_input_frame = sizeof(sample_t) * drv->num_input_channels;

  if(drv->num_output_channels > 0)
  {
    drv->pPlayPtr = jack_ringbuffer_create(drv->num_output_channels *
                                           drv->bytes_per_jack_output_frame *
                                           DEFAULT_RB_SIZE);
    jack_ringbuffer_mlock( drv->pPlayPtr );
  }

  if(drv->num_input_channels > 0)
  {
      drv->pRecPtr = jack_ringbuffer_create(drv->num_input_channels *
                                            drv->bytes_per_jack_input_frame *
                                            DEFAULT_RB_SIZE);
	  jack_ringbuffer_mlock( drv->pPlayPtr );
  }

  DEBUG("bytes_per_output_frame == %ld", drv->bytes_per_output_frame);
  DEBUG("bytes_per_input_frame  == %ld", drv->bytes_per_input_frame);
  DEBUG("bytes_per_jack_output_frame == %ld",
        drv->bytes_per_jack_output_frame);
  DEBUG("bytes_per_jack_input_frame == %ld",
        drv->bytes_per_jack_input_frame);

  /* go and open up the device */
  retval = JACK_OpenDevice(drv);
  if(retval != ERR_SUCCESS)
  {
    pthread_mutex_unlock(&device_mutex);
    return retval;
  }

  drv->allocated = TRUE;        /* record that we opened this device */

  DEBUG("sizeof(sample_t) == %d", sizeof(sample_t));

  int periodSize = jack_get_buffer_size(drv->client);
  int periods = 0;
  if(drv->num_output_channels > 0)
  {
#ifdef HAVE_JACK_PORT_GET_LATENCY_RANGE
	jack_latency_range_t r;
	jack_port_get_latency_range( drv->output_port[0], JackCaptureLatency, &r );
	periods += r.max;
#else
    periods = jack_port_get_total_latency(drv->client,
                                          drv->output_port[0]) / periodSize;
#endif
	drv->latencyMS = periodSize * periods * 1000 / (drv->jack_sample_rate *
                                                    (drv->bits_per_channel / 8 *
                                                     drv->num_output_channels));
  }
  else if(drv->num_input_channels > 0)
  {
#ifdef HAVE_JACK_PORT_GET_LATENCY_RANGE
	jack_latency_range_t r;
	jack_port_get_latency_range( drv->input_port[0], JackCaptureLatency, &r );
	periods += r.max;
#else

    periods = jack_port_get_total_latency(drv->client,
                                          drv->input_port[0]) / periodSize;
#endif
    drv->latencyMS =
      periodSize * periods * 1000 / (drv->jack_sample_rate *
                                     (drv->bits_per_channel / 8 *
                                      drv->num_input_channels));
  }

  DEBUG("latency is %ldms", drv->latencyMS);

  *deviceID = drv->deviceID;    /* set the deviceID for the caller */
  pthread_mutex_unlock(&device_mutex);
  return ERR_SUCCESS;           /* success */
}

/* Close the jack device */
//FIXME: add error handling in here at some point...
/* NOTE: return 0 for success, non-zero for failure */
int
JACK_Close(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);

#if JACK_CLOSE_HACK
  JACK_CloseDevice(drv, TRUE);
#else
  JACK_CloseDevice(drv);
#endif

  JACK_ResetFromDriver(drv);    /* reset this device to a normal starting state */

  pthread_mutex_lock(&device_mutex);

  /* free buffer memory */
  drv->callback_buffer1_size = 0;
  if(drv->callback_buffer1) free(drv->callback_buffer1);
  drv->callback_buffer1 = 0;

  drv->callback_buffer2_size = 0;
  if(drv->callback_buffer2) free(drv->callback_buffer2);
  drv->callback_buffer2 = 0;

  drv->rw_buffer1_size = 0;
  if(drv->rw_buffer1) free(drv->rw_buffer1);
  drv->rw_buffer1 = 0;

  if(drv->pPlayPtr) jack_ringbuffer_free(drv->pPlayPtr);
  drv->pPlayPtr = 0;

  if(drv->pRecPtr) jack_ringbuffer_free(drv->pRecPtr);
  drv->pRecPtr = 0;

  /* free the SRC objects */
  drv->allocated = FALSE;       /* release this device */

  pthread_mutex_unlock(&device_mutex);

  return 0;
}

/* If we haven't already taken in the max allowed data then create a wave header */
/* to package the audio data and attach the wave header to the end of the */
/* linked list of wave headers */
/* These wave headers will be peeled off as they are played by the callback routine */
/* Return value is the number of bytes written */
/* NOTE: this function takes the length of data to be written bytes */
long
JACK_Write(int deviceID, unsigned char *data, unsigned long bytes)
{
  jack_driver_t *drv = getDriver(deviceID);

  long frames_free, frames;

  /* check and see that we have enough space for this audio */
  frames_free =
    jack_ringbuffer_write_space(drv->pPlayPtr) /
    drv->bytes_per_jack_output_frame;
  frames = bytes / drv->bytes_per_output_frame;
  /* if we are currently STOPPED we should start playing now... 
     do this before the check for bytes == 0 since some clients like
     to write 0 bytes the first time out */
  if(drv->state == STOPPED)
  {
    DEBUG("currently STOPPED, transitioning to PLAYING");
    drv->state = PLAYING;
  }

  /* handle the case where the user calls this routine with 0 bytes */
  if(bytes == 0 || frames_free < 1)
  {
    return 0;                   /* indicate that we couldn't write any bytes */
  }

  frames = min(frames, frames_free);
  long jack_bytes = frames * drv->bytes_per_jack_output_frame;
  if(!ensure_buffer_size(&drv->rw_buffer1, &drv->rw_buffer1_size, jack_bytes))
  {
    ERR("couldn't allocate enough space for the buffer");
    return 0;
  }
  /* adjust bytes to be how many client bytes we're actually writing */
  bytes = frames * drv->bytes_per_output_frame;

  /* convert from client samples to jack samples
     we have to tell it how many samples there are, which is frames * channels */
  switch (drv->bits_per_channel)
  {
  case 8:
    sample_move_char_float((sample_t *) drv->rw_buffer1, (unsigned char *) data,
                           frames * drv->num_output_channels);
    break;
  case 16:
    sample_move_short_float((sample_t *) drv->rw_buffer1, (short *) data,
                            frames * drv->num_output_channels);
    break;
  }

/*  DEBUG("ringbuffer read space = %d, write space = %d",
        jack_ringbuffer_read_space(drv->pPlayPtr),
        jack_ringbuffer_write_space(drv->pPlayPtr));
*/
  jack_ringbuffer_write(drv->pPlayPtr, drv->rw_buffer1, jack_bytes);
/*  DEBUG("wrote %lu bytes, %lu jack_bytes", bytes, jack_bytes);

  DEBUG("ringbuffer read space = %d, write space = %d",
        jack_ringbuffer_read_space(drv->pPlayPtr),
        jack_ringbuffer_write_space(drv->pPlayPtr));
*/
  drv->client_bytes += bytes;   /* update client_bytes */

//  DEBUG("returning bytes written of %ld", bytes);

  return bytes;                 /* return the number of bytes we wrote out */
}

long
JACK_Read(int deviceID, unsigned char *data, unsigned long bytes)
{
  jack_driver_t *drv = getDriver(deviceID);

  long frames_available, frames;

  /* find out if there's any room to write this data */
  frames_available =
    jack_ringbuffer_read_space(drv->pRecPtr) /
    drv->bytes_per_jack_input_frame;
  frames = bytes / drv->bytes_per_input_frame;
//  DEBUG("frames available = %ld, bytes = %lu", frames_available, bytes);

  /* if we are currently STOPPED we should start recording now... */
  if(drv->state == STOPPED)
  {
    DEBUG("currently STOPPED, transitioning to PLAYING");
    drv->state = PLAYING;
  }

  /* handle the case where the user calls this routine with 0 bytes */
  if(bytes == 0 || frames_available < 1)
  {
    DEBUG("no bytes in buffer");
    return 0;
  }

  frames = min(frames, frames_available);
  long jack_bytes = frames * drv->bytes_per_jack_input_frame;
  if(!ensure_buffer_size(&drv->rw_buffer1, &drv->rw_buffer1_size, jack_bytes))
  {
    ERR("couldn't allocate enough space for the buffer");
    return 0;
  }

/*  DEBUG("ringbuffer read space = %d, write space = %d"
        jack_ringbuffer_read_space(drv->pRecPtr),
        jack_ringbuffer_write_space(drv->pRecPtr));
*/
  jack_ringbuffer_read(drv->pRecPtr, drv->rw_buffer1,
                       frames * drv->bytes_per_jack_input_frame);
/*
  DEBUG("ringbuffer read space = %d, write space = %d",
        jack_ringbuffer_read_space(drv->pRecPtr),
        jack_ringbuffer_write_space(drv->pRecPtr));
*/
  unsigned int i;
  for(i = 0; i < drv->num_output_channels; i++)
  {
    /* apply volume to the floating value */
    if(drv->volumeEffectType == dbAttenuation)
    {
      /* assume the volume setting is dB of attenuation, a volume of 0 */
      /* is 0dB attenuation */
      float volume = powf(10.0, -((float) drv->volume[i]) / 20.0);
      float_volume_effect((sample_t *) drv->rw_buffer1 + i,
                          frames, volume, drv->num_output_channels);
    } else
    {
      float_volume_effect((sample_t *) drv->rw_buffer1 + i, frames,
                          ((float) drv->volume[i] / 100.0),
                          drv->num_output_channels);
    }
  }

  /* convert from jack samples to client samples
     we have to tell it how many samples there are, which is frames * channels */
  switch (drv->bits_per_channel)
  {
  case 8:
    sample_move_float_char((unsigned char *) data, (sample_t *) drv->rw_buffer1,
                           frames * drv->num_input_channels);
    break;
  case 16:
    sample_move_float_short((short *) data, (sample_t *) drv->rw_buffer1,
                            frames * drv->num_input_channels);
    break;
  }

  long read_bytes = frames * drv->bytes_per_input_frame;

//  DEBUG("returning bytes read of %ld", bytes);

  return read_bytes;
}

/* return ERR_SUCCESS for success */
static int
JACK_SetVolumeForChannelFromDriver(jack_driver_t * drv,
                                   unsigned int channel, unsigned int volume)
{
  /* TODO?: maybe we should have different volume levels for input & output */
  /* ensure that we have the channel we are setting volume for */
  if(channel > (drv->num_output_channels - 1))
    return 1;

  if(volume > 100)
    volume = 100;               /* check for values in excess of max */

  drv->volume[channel] = volume;
  return ERR_SUCCESS;
}

/* return ERR_SUCCESS for success */
int
JACK_SetVolumeForChannel(int deviceID, unsigned int channel,
                         unsigned int volume)
{
  jack_driver_t *drv = getDriver(deviceID);
  int retval = JACK_SetVolumeForChannelFromDriver(drv, channel, volume);
  return retval;
}

/* Set the volume */
/* return 0 for success */
/* NOTE: we check for invalid volume values */
int
JACK_SetAllVolume(int deviceID, unsigned int volume)
{
  jack_driver_t *drv = getDriver(deviceID);
  unsigned int i;

  for(i = 0; i < drv->num_output_channels; i++)
  {
    if(JACK_SetVolumeForChannelFromDriver(drv, i, volume) != ERR_SUCCESS)
    {
      return 1;
    }
  }

  return ERR_SUCCESS;
}

/* Return the current volume in the inputted pointers */
/* NOTE: we check for null pointers being passed in just in case */
void
JACK_GetVolumeForChannel(int deviceID, unsigned int channel,
                         unsigned int *volume)
{
  jack_driver_t *drv = getDriver(deviceID);

  /* ensure that we have the channel we are getting volume for */
  if(channel > (drv->num_output_channels - 1))
  {
    ERR("asking for channel index %d but we only have %ld channels", channel, drv->num_output_channels);
    return;
  }

  if(volume)
    *volume = drv->volume[channel];

}


/* linear means 0 volume is silence, 100 is full volume */
/* dbAttenuation means 0 volume is 0dB attenuation */
/* Bio2jack defaults to linear */
enum JACK_VOLUME_TYPE
JACK_SetVolumeEffectType(int deviceID, enum JACK_VOLUME_TYPE type)
{
  enum JACK_VOLUME_TYPE retval;
  jack_driver_t *drv = getDriver(deviceID);

  DEBUG("setting type of '%s'",
        (type == dbAttenuation ? "dbAttenuation" : "linear"));

  retval = drv->volumeEffectType;
  drv->volumeEffectType = type;

  return retval;
}


/* Controls the state of the playback(playing, paused, ...) */
int
JACK_SetState(int deviceID, enum status_enum state)
{
  jack_driver_t *drv = getDriver(deviceID);

  switch (state)
  {
  case PAUSED:
    drv->state = PAUSED;
    break;
  case PLAYING:
    drv->state = PLAYING;
    break;
  case STOPPED:
    drv->state = STOPPED;
    break;
  default:
    DEBUG("unknown state of %d", state);
  }

  if(drv->state == PAUSED  ||
          drv->state == STOPPED ||
          drv->state == CLOSED  || drv->state == RESET)
  {
    /* if we were told to reset then zero out some variables */
    /* and transition to STOPPED */
    if(drv->state == RESET)
    {
      drv->written_client_bytes = 0;
      drv->played_client_bytes = 0;     /* number of the clients bytes that jack has played */

      drv->client_bytes = 0;    /* bytes that the client wrote to use */

      drv->clientBytesInJack = 0;       /* number of input bytes in jack(not necessary the number of bytes written to jack) */

      drv->position_byte_offset = 0;

      if(drv->pPlayPtr)
        jack_ringbuffer_reset(drv->pPlayPtr);

      if(drv->pRecPtr)
        jack_ringbuffer_reset(drv->pRecPtr);

      drv->state = STOPPED;     /* transition to STOPPED */
    }
  }

  return 0;
}

/* Retrieve the current state of the device */
enum status_enum
JACK_GetState(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  enum status_enum return_val;

  return_val = drv->state;

  return return_val;
}

/* Retrieve the number of bytes per second we are outputting */
unsigned long
JACK_GetOutputBytesPerSecondFromDriver(jack_driver_t * drv)
{
  unsigned long return_val;

  return_val = drv->bytes_per_output_frame * drv->client_sample_rate;

  return return_val;
}

/* Retrieve the number of bytes per second we are outputting */
unsigned long
JACK_GetOutputBytesPerSecond(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  unsigned long return_val;

  return_val = JACK_GetOutputBytesPerSecondFromDriver(drv);

  return return_val;
}

/* Retrieve the number of input bytes(from jack) per second we are outputting
   to the user of bio2jack */
static long
JACK_GetInputBytesPerSecondFromDriver(jack_driver_t * drv)
{
  long return_val;

  return_val = drv->bytes_per_input_frame * drv->client_sample_rate;

  return return_val;
}

/* Retrieve the number of input bytes(from jack) per second we are outputting
   to the user of bio2jack */
unsigned long
JACK_GetInputBytesPerSecond(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  long return_val = JACK_GetInputBytesPerSecondFromDriver(drv);

  return return_val;
}

/* Return the number of bytes we have buffered thus far for output */
/* NOTE: convert from output bytes to input bytes in here */
static long
JACK_GetBytesStoredFromDriver(jack_driver_t * drv)
{
  if(drv->pPlayPtr == 0 || drv->bytes_per_jack_output_frame == 0)
    return 0;

  /* leave at least one frame in the buffer at all times to prevent underruns */
  long return_val =
    jack_ringbuffer_read_space(drv->pPlayPtr) - drv->jack_buffer_size;
  if(return_val <= 0)
  {
    return_val = 0;
  } else
  {
    /* adjust from jack bytes to client bytes */
    return_val =
      return_val / drv->bytes_per_jack_output_frame *
      drv->bytes_per_output_frame;
  }

  return return_val;
}

/* An approximation of how many bytes we have to send out to jack */
/* that is computed as if we were sending jack a continuous stream of */
/* bytes rather than chunks during discrete callbacks.  */
/* Return the number of bytes we have buffered thus far for output */
/* NOTE: convert from output bytes to input bytes in here */
unsigned long
JACK_GetBytesStored(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  return (unsigned long) JACK_GetBytesStoredFromDriver(drv);
}

static unsigned long
JACK_GetBytesFreeSpaceFromDriver(jack_driver_t * drv)
{
  if(drv->pPlayPtr == 0 || drv->bytes_per_jack_output_frame == 0)
    return 0;

  /* leave at least one frame in the buffer at all times to prevent underruns */
  long return_val = jack_ringbuffer_write_space(drv->pPlayPtr) - drv->jack_buffer_size;
  if(return_val <= 0)
  {
    return_val = 0;
  } else
  {
    /* adjust from jack bytes to client bytes */
    return_val =
      return_val / drv->bytes_per_jack_output_frame *
      drv->bytes_per_output_frame;
  }

  return return_val;
}

/* Return the number of bytes we can write to the device */
unsigned long
JACK_GetBytesFreeSpace(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  return (unsigned long) JACK_GetBytesFreeSpaceFromDriver(drv);
}

/* bytes of space used in the input buffer */
unsigned long
JACK_GetBytesUsedSpace(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  long return_val;

  if(drv->pRecPtr == 0 || drv->bytes_per_jack_input_frame == 0)
  {
    return_val = 0;
  } else
  {
    /* adjust from jack bytes to client bytes */
    return_val =
      jack_ringbuffer_read_space(drv->pRecPtr) /
      drv->bytes_per_jack_input_frame * drv->bytes_per_input_frame;
  }

  if(return_val < 0)
    return_val = 0;

  return return_val;
}

/* Get the current position of the driver, either in bytes or */
/* in milliseconds */
/* NOTE: this is position relative to input bytes, output bytes may differ greatly due to
   input vs. output channel count */
/* veejay: we do not use this function */
static long
JACK_GetPositionFromDriver(jack_driver_t * drv, enum pos_enum position,
                           int type)
{
  long return_val = 0;
  struct timespec now;
  long elapsedMS;
  double sec2msFactor = 1000;

  //char *type_str = "UNKNOWN type";

  /* if we are reset we should return a position of 0 */
  if(drv->state == RESET)
  {
    DEBUG("we are currently RESET, returning 0");
    return 0;
  }

  if(type == WRITTEN)
  {
 //   type_str = "WRITTEN";
    return_val = drv->client_bytes;
  } else if(type == WRITTEN_TO_JACK)
  {
//    type_str = "WRITTEN_TO_JACK";
    return_val = drv->written_client_bytes;
  } else if(type == PLAYED)       /* account for the elapsed time for the played_bytes */
  {
//    type_str = "PLAYED";
    return_val = drv->played_client_bytes;
//    gettimeofday(&now, 0);
    clock_gettime( CLOCK_REALTIME, &now );

    elapsedMS = TimeValDifference(&drv->previousTime, &now);    /* find the elapsed milliseconds since last JACK_Callback() */

    DEBUG("elapsedMS since last callback is '%ld'", elapsedMS);

    /* account for the bytes played since the last JACK_Callback() */
    /* NOTE: [Xms * (Bytes/Sec)] * (1 sec/1,000ms) */
    /* NOTE: don't do any compensation if no data has been sent to jack since the last callback */
    /* as this would result a bogus computed result */
    if(drv->clientBytesInJack != 0)
    {
      return_val += (long) ((double) elapsedMS *
                            ((double) JACK_GetOutputBytesPerSecondFromDriver(drv) /
                             sec2msFactor));
    } 
  }

  /* add on the offset */
  return_val += drv->position_byte_offset;

  /* convert byte position to milliseconds value if necessary */
  if(position == MILLISECONDS)
  {
    if(JACK_GetOutputBytesPerSecondFromDriver(drv) != 0)
    {
      return_val = (long) (((double) return_val /
                            (double) JACK_GetOutputBytesPerSecondFromDriver(drv)) *
                           (double) sec2msFactor);
    } else
    {
      return_val = 0;
    }
  }

  return return_val;
}

/* Get the current position of the driver, either in bytes or */
/* in milliseconds */
/* NOTE: this is position relative to input bytes, output bytes may differ greatly due to input vs. output channel count */
long
JACK_GetPosition(int deviceID, enum pos_enum position, int type)
{
  jack_driver_t *drv = getDriver(deviceID);
  return (long) JACK_GetPositionFromDriver(drv, position, type);
}

// Set position always applies to written bytes
// NOTE: we must apply this instantly because if we pass this as a message
//   to the callback we risk the user sending us audio data in the mean time
//   and there is no need to send this as a message, we don't modify any
//   internal variables
void
JACK_SetPositionFromDriver(jack_driver_t * drv, enum pos_enum position,
                           long value)
{
  double sec2msFactor = 1000;

  /* convert the incoming value from milliseconds into bytes */
  if(position == MILLISECONDS)
  {
    value = (long) (((double) value *
                     (double) JACK_GetOutputBytesPerSecondFromDriver(drv)) /
                    sec2msFactor);
  }

  /* ensure that if the user asks for the position */
  /* they will at this instant get the correct position */
  drv->position_byte_offset = value - drv->client_bytes;

}

// Set position always applies to written bytes
// NOTE: we must apply this instantly because if we pass this as a message
//   to the callback we risk the user sending us audio data in the mean time
//   and there is no need to send this as a message, we don't modify any
//   internal variables
void
JACK_SetPosition(int deviceID, enum pos_enum position, long value)
{
  jack_driver_t *drv = getDriver(deviceID);
  JACK_SetPositionFromDriver(drv, position, value);
}

/* Return the number of bytes per frame, or (output_channels * bits_per_channel) / 8 */
unsigned long
JACK_GetBytesPerOutputFrame(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  return (unsigned long) drv->bytes_per_output_frame;
}

/* Return the number of bytes per frame, or (input_channels * bits_per_channel) / 8 */
unsigned long
JACK_GetBytesPerInputFrame(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  return (unsigned long) drv->bytes_per_input_frame;
}

/* Return the number of output bytes we buffer max */
long
JACK_GetMaxOutputBufferedBytes(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  long return_val;

  if(drv->pPlayPtr == 0 || drv->bytes_per_jack_output_frame == 0) return_val = 0;

  /* adjust from jack bytes to client bytes */
  return_val =
    (jack_ringbuffer_read_space(drv->pPlayPtr) +
     jack_ringbuffer_write_space(drv->pPlayPtr)) /
    drv->bytes_per_jack_output_frame * drv->bytes_per_output_frame;

  return return_val;
}


/* Reset ringbuffer */
void
JACK_ResetBuffer(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);

  if(drv->pPlayPtr) {
	  jack_ringbuffer_reset( drv->pPlayPtr );
  }
}



/* Return the number of input bytes we buffer max */
long
JACK_GetMaxInputBufferedBytes(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  long return_val;

  if(drv->pRecPtr == 0 || drv->bytes_per_jack_input_frame == 0) return_val = 0;

  /* adjust from jack bytes to client bytes */
  return_val =
    (jack_ringbuffer_read_space(drv->pRecPtr) +
     jack_ringbuffer_write_space(drv->pRecPtr)) /
    drv->bytes_per_jack_input_frame * drv->bytes_per_input_frame;

  return return_val;
}

/* Get the number of output channels */
int
JACK_GetNumOutputChannels(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  return drv->num_output_channels;
}

/* Get the number of input channels */
int
JACK_GetNumInputChannels(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  return drv->num_input_channels;
}

/* Get the number of samples per second, the sample rate */
long
JACK_GetSampleRate(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  return drv->client_sample_rate;
}

void
JACK_CleanupDriver(jack_driver_t * drv)
{
  /* things that need to be reset both in JACK_Init & JACK_CloseDevice */
  drv->client = 0;
  drv->in_use = FALSE;
  drv->state = CLOSED;
  drv->jack_sample_rate = 0;
  drv->output_sample_rate_ratio = 1.0;
  drv->input_sample_rate_ratio = 1.0;
  drv->jackd_died = FALSE;
  clock_gettime(CLOCK_REALTIME, &drv->previousTime);  /* record the current time */
  memcpy( &drv->last_reconnect_attempt, &drv->previousTime, sizeof(struct timespec));
}


/* Initialize the jack porting library to a clean state */
void
JACK_Init(void)
{
  jack_driver_t *drv;
  int x, y;

  if(init_done)
  {
    DEBUG("not initing twice");
    return;
  }

  init_done = 1;

  pthread_mutex_lock(&device_mutex);

  /* initialize the device structures */
  for(x = 0; x < MAX_OUTDEVICES; x++)
  {
    drv = &outDev[x];

    getDriver(x);

    memset(drv, 0, sizeof(jack_driver_t));
    drv->volumeEffectType = linear;
    drv->deviceID = x;

    for(y = 0; y < MAX_OUTPUT_PORTS; y++)       /* make all volume 25% as a default */
      drv->volume[y] = DEFAULT_VOLUME;

    JACK_CleanupDriver(drv);
    JACK_ResetFromDriver(drv);
  }

  client_name = 0;              /* initialize the name to null */
  //@ veejay: default to false!
  JACK_SetClientName("bio2jack");

  pthread_mutex_unlock(&device_mutex);

}

/* Get the latency, in frames, of jack */
long
JACK_GetJackOutputLatency(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  long return_val = 0;

  if(drv->client && drv->num_output_channels) {
#ifdef HAVE_JACK_PORT_GET_LATENCY_RANGE
	jack_latency_range_t r;
	jack_port_get_latency_range( drv->output_port[0], JackCaptureLatency, &r );
	return_val = r.max;
#else
 	return_val = jack_port_get_total_latency(drv->client, drv->output_port[0]);
#endif
  }
  return return_val;
}

/* Get the latency, in frames, of jack */
long
JACK_GetJackInputLatency(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  long return_val = 0;

  if(drv->client && drv->num_input_channels) {
#ifdef HAVE_JACK_PORT_GET_LATENCY_RANGE
	jack_latency_range_t r;
	jack_port_get_latency_range( drv->input_port[0], JackCaptureLatency, &r );
	return_val = r.max;
#else
    return_val = jack_port_get_total_latency(drv->client, drv->input_port[0]);
#endif
  }

  return return_val;
}

/* bytes that jack requests during each callback */
unsigned long
JACK_GetJackBufferedBytes(int deviceID)
{
  jack_driver_t *drv = getDriver(deviceID);
  long return_val;

  if(drv->bytes_per_jack_output_frame == 0)
  {
    return_val = 0;
  } else
  {
    /* adjust from jack bytes to client bytes */
    return_val =
      drv->jack_buffer_size / drv->bytes_per_jack_output_frame *
      drv->bytes_per_output_frame * drv->num_output_channels;
  }

  return return_val;
}

/* value = TRUE, perform sample rate conversion */
void
JACK_DoSampleRateConversion(bool value)
{
}

/* FIXME: put the filename of the resample library header file with the decoders in here */
/* consider mapping them in the bio2jack.h header file since its useless to the user unless */
/* they can figure out wtf the settings on */
void
JACK_SetSampleRateConversionFunction(int converter)
{
}

/* set the client name that will be reported to jack when we open a */
/* connection via JACK_OpenDevice() */
void
JACK_SetClientName(char *name)
{
  if(name)
  {
    if(client_name) free(client_name);

    /* jack_client_name_size() is the max length of a client name, including
       the terminating null. */
    int size = strlen(name) + 1;        /* take into account the terminating null */
    if(size > jack_client_name_size())
      size = jack_client_name_size();

    client_name = malloc(size);
    if(client_name)
      snprintf(client_name, size, "%s", name);
    else
      ERR("unable to allocate %d bytes for client_name", size);
  }
}

void
JACK_FreeClientName()
{
	if( client_name ) {
		free(client_name);
		client_name = NULL;
	}
}


long JACK_OutputStatus(int deviceID,long *sec, long *usec)
{
  	jack_driver_t *this = &outDev[deviceID];
	*sec =	this->previousTime.tv_sec;
	*usec = this->previousTime.tv_nsec;

	//	return (this->ticks * this->chunk_size); 
	return ( this->written_client_bytes / this->bytes_per_output_frame);
}



void
JACK_SetPortConnectionMode(enum JACK_PORT_CONNECTION_MODE mode)
{
    port_connection_mode = mode;
}
#endif
