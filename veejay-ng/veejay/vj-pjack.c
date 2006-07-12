/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <elburg@hio.hen.nl> 
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

/** \defgroup Jack Jackit Client
 */

#include <config.h>


#ifdef HAVE_JACK
#include <bio2jack/bio2jack.h>
#include <libel/vj-el.h>
static int driver = 0;
static int bits_per_sample = 0;
static unsigned long audio_rate = 0;
static int audio_channels = 0;
static int audio_bps = 0;
static int buffer_len = 0;
static unsigned long v_rate = 0;

extern void veejay_msg(int type, const char format[], ...);

int vj_jack_initialize()
{
	JACK_Init();
	return 0;
}
static int _vj_jack_start(int *dri)
{
	const char *port_name = NULL;
	unsigned long port_flags = JackPortIsPhysical;
	unsigned int port_count=0;
	int err = JACK_OpenEx(dri, bits_per_sample,&audio_rate,0,audio_channels, &port_name, port_count, port_flags);
	switch(err)
	{
		case ERR_TOO_MANY_OUTPUT_CHANNELS:
		veejay_msg(0, "(JACK) Too many output channels");
		break;
		case ERR_PORT_NAME_OUTPUT_CHANNEL_MISMATCH:
		veejay_msg(0, "(JACK) Mismatched output channel port name");
		break;
		case ERR_BYTES_PER_OUTPUT_FRAME_INVALID:
		veejay_msg(0, "(JACK) Bytes per output frame invalid");
		break;
		case ERR_RATE_MISMATCH:
		veejay_msg(0, "(JACK) Audio rate mismatch");
		break;
		case ERR_OPENING_JACK:
		veejay_msg(0, "Is the device already used?");
		break;
		case ERR_SUCCESS:
		veejay_msg(2, "Successfully connected to Jackd");
		break;
	}		
	if(err != ERR_SUCCESS)
	{
		return 0;
	}

	return 1;
}

int vj_jack_init(int bits_ps, int audio_chans, int rate)
{
	int err;
	int i = 0;
	int ret = 0;

	audio_bps = audio_rate * audio_channels;
	audio_rate = rate;
	audio_channels = audio_chans;
	bits_per_sample = bits_ps;
	buffer_len = vj_jack_get_space();
	if( !_vj_jack_start(&driver) )
		return 0;


	veejay_msg(2,"Sample has Audio,  %d Hz/ %d Channels %d Bit ", audio_rate, audio_channels,bits_per_sample);
	return 1;
}

void	vj_jack_continue(int speed)
{
	if(speed==0)
	{
		if(JACK_GetState(driver) == PAUSED) return;
		JACK_SetState(driver, PAUSED );
	}
	else
	if( JACK_GetState(driver) == PAUSED )
	{
		JACK_SetState(driver, PLAYING);
	}
}


int	vj_jack_stop()
{

	JACK_Reset( driver );

	if(JACK_Close(driver))
	{
		veejay_msg(2,
			"(Jack) Error closing device");
	}
	return 1;
}

int	vj_jack_reset()
{
	veejay_msg(0, "%s: RESET",__FUNCTION__ );
	JACK_Reset(driver);
	buffer_len = 0;
	return 1;
}

int	vj_jack_c_play(void *data, int len, int entry)
{
	return 0;
}

int	vj_jack_play(void *data, int len)
{
	return  JACK_Write(driver,data,len);
}

int	vj_jack_set_volume(int volume)
{
	if(JACK_SetAllVolume( driver, volume)==0)
		return 1;
	return 0;
}

int	vj_jack_pause()
{
	JACK_SetState(driver,PAUSED);
	return 1;
}

int	vj_jack_resume()
{
	JACK_SetState(driver,PLAYING);
	return 1;
}

int	vj_jack_get_space()
{
	return JACK_GetBytesFreeSpace(driver);
}

long	vj_jack_get_status(long int *sec, long int *usec)
{
//JACK_GetPosition(int deviceID, enum pos_enum position, int type);

	return JACK_OutputStatus( driver , sec, usec ) + JACK_GetJackOutputLatency( driver ) + (JACK_GetJackBufferedBytes(driver) * 2);

}
#endif
