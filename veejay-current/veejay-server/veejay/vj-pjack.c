/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
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

#include <config.h>
#ifdef HAVE_JACK
#include <stdint.h>
#include <sys/time.h>
#include <veejaycore/defs.h>
#include <bio2jack/bio2jack.h>
#include <libel/vj-el.h>
static int driver = 0;

extern void veejay_msg(int type, const char format[], ...);

int vj_jack_initialize(void)
{
	JACK_Init();
	JACK_SetClientName("veejay");
	return 0;
}
static int _vj_jack_start(int *dri, int bits_per_sample, long audio_rate, int audio_channels, double SPVF)
{
	int err = JACK_Open(dri, (bits_per_sample * 8) / audio_channels,&audio_rate,audio_channels, SPVF);
	if(err == ERR_RATE_MISMATCH)
	{
		veejay_msg(1, "(Jackd) Sample rate mismatch (Retrying)");

		JACK_Close( *dri );

		err = JACK_Open(dri, (bits_per_sample * 8) / audio_channels,&audio_rate, audio_channels, SPVF);
	}

	if(err != ERR_SUCCESS)
	{
		switch(err)
		{
			case ERR_OPENING_JACK:
			  veejay_msg(0, "Unable to make a connection with Jack" );
			  break;
			case ERR_RATE_MISMATCH:
			  veejay_msg(0, "Jackd cannot handle samplerate of %d Hz", audio_rate);
			  break;
			case ERR_TOO_MANY_OUTPUT_CHANNELS:	
			  veejay_msg(0, "Cannot connect to jackd, Too many output channels: %d",
				audio_channels );
			  break;
			case ERR_PORT_NOT_FOUND:
			   veejay_msg(0, "Unable to find jack port");
			   break;
		}
		
		JACK_Close( *dri );
//		veejay_msg(0, "To run veejay without audio, use -a0");
		return 0;
	}

	return 1;
}


int vj_jack_init(editlist *el)
{
	if( !_vj_jack_start(&driver, el->audio_bps, el->audio_rate, el->audio_chans,1.0 / (double) el->video_fps) )
		return 0;

	long jack_rate = JACK_GetSampleRate(driver );

	if( jack_rate != el->audio_rate ) {
		veejay_msg(2,"May be wrong but bio2jack client reports a different sample rate: %ld instead of %d", jack_rate,el->audio_rate );
	}

	return 1;
}

int	vj_jack_get_client_samplerate(void)
{
	return JACK_GetSampleRate(driver);
}

int	vj_jack_stop(void)
{
	if( JACK_GetState(driver) == PLAYING )
		JACK_SetState(driver,STOPPED);

	JACK_Reset( driver );

	if(JACK_Close(driver))
	{
		veejay_msg(2,
			"(Jack) Error closing device");
	}
	return 1;
}

long vj_jack_underruns(void) {
	return JACK_GetUnderruns(driver);
}

int vj_jack_xrun_flag(void) {
	return JACK_XRUNHandled(driver);
}

void	vj_jack_enable(void)
{
	JACK_SetState(driver, PLAYING );
}

void	vj_jack_disable(void)
{
	JACK_SetState( driver, STOPPED );
}

int	vj_jack_c_play(void *data, int len, int entry)
{
	return 0;
}

int vj_jack_play(void *data, int len)
{
    int frames = JACK_Write( driver, data, len );
    
    // Original error-handling logic
    if( frames == 0 && JACK_GetState(driver) == PLAYING ) {
        vj_jack_disable();
		veejay_msg(0, "Error writing to JACK!");
    }
    return frames;
}

int vj_jack_get_rate(void) {
	return JACK_GetSampleRateJack(driver);
}

int	vj_jack_play2(void *data, int len)
{
	int res = JACK_Write( driver, data, len );
	if( res == 0 && JACK_GetState(driver) == PLAYING ) {
		vj_jack_disable();
	}
	return res;
}

void vj_jack_debug(int skipv, int skipa) {
	
}

int	vj_jack_set_volume(int volume)
{
	if(JACK_SetAllVolume( driver, volume)==0)
		return 1;
	return 0;
}

void vj_jack_flush(void) {
	JACK_Flush(driver);
}

int	vj_jack_pause(void)
{
	JACK_SetState(driver,PAUSED);
	return 1;
}

int	vj_jack_resume(void)
{
	JACK_SetState(driver,PLAYING);
	return 1;
}

int	vj_jack_get_space(void)
{
	return JACK_GetBytesFreeSpace(driver);
}

long	vj_jack_get_status(long int *sec, long int *nsec)
{
	return JACK_OutputStatus( driver, sec, nsec);
}

double	vj_jack_get_played_position(void) {

	return (driver);
}

unsigned long vj_jack_get_played_frames(void) {
	return JACK_GetPlayedFramesFromDriver(driver);
}

long    vj_jack_get_written_frames(void) {
	return JACK_GetPosition(driver,0,FRAMES_WRITTEN_TO_JACK);
}

void	vj_jack_reset(void)
{
	JACK_ResetBuffer( driver );
}

int	vj_jack_is_starving(void) {
	return JACK_BufferIsStarving(driver);
}

int	vj_jack_get_period_size(void) {
	return JACK_GetPeriodSize(driver);
}

int	vj_jack_get_ringbuffer_frames_free(void) {
	return JACK_GetRingBufferFreeFrames(driver);
}

int vj_jack_get_ringbuffer_size(void) {
	return JACK_GetRingBufferSize(driver);
}

long vj_jack_get_ringbuffer_used(void) {
	return JACK_GetRingBufferUsed(driver);
}

int vj_jack_client_to_jack_frames(int client_frames) {
	return JACK_GetClientToJackFrames(driver,client_frames);
}

long vj_jack_get_required_free_frames(int client_frames) {
	return JACK_GetRequiredFreeFrames(driver,client_frames);
}

unsigned long vj_jack_get_bytes_per_frame(void) {
	return JACK_GetBytesPerOutputFrame(driver);
}

long vj_jack_get_bytes_stored_from_driver(void) {
	return JACK_GetBytesStored(driver);

}

int vj_jack_is_stopped(void) {
	return JACK_GetState(driver) == STOPPED;
}
int vj_jack_is_playing(void) {
	return JACK_GetState(driver) == PLAYING;
}

int vj_jack_is_callback_active(void) {
	return JACK_GetCallbackActive(driver);
}

double vj_jack_get_total_latency(void) {
	return JACK_GetTotalLatency(driver);
}


int vj_jack_is_running(void) {
    return (driver == 0 ? 0 : 1);
}

#endif
