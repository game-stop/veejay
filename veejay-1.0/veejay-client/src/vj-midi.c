/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2007 Niels Elburg <nelburg@looze.net> 
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
#include <veejay/vjmem.h>
#include <veejay/vj-msg.h>
#include <alsa/asoundlib.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <veejay/libvevo.h>
#include <veejay/vevo.h>
#include <src/vj-api.h>

extern GtkWidget      *glade_xml_get_widget_( GladeXML *m, const char *name );
extern void    msg_vims(char *message);
extern void    vj_msg(int type, const char format[], ...);

typedef struct
{
	snd_seq_t	*sequencer;
	void		*vims;
	struct pollfd	*pfd;
	int		npfd;
	int		learn;
	int		learn_event[4];
	void		*mw;
} vmidi_t;

typedef struct
{
	int		extra;
	char		*widget;
	char		*msg;
} dvims_t;

void	vj_midi_learn( void *vv )
{
	vmidi_t *v = (vmidi_t*) vv;
	v->learn = 1;
	vj_msg(VEEJAY_MSG_INFO, "Learning MIDI commands. Touch a midi key and then click a widget");
}

void	vj_midi_play(void *vv )
{
	vmidi_t *v = (vmidi_t*) vv;
	v->learn = 0;
	vj_msg(VEEJAY_MSG_INFO, "MIDI listener active");
}

void	vj_midi_load(void *vv, const char *filename)
{
	vmidi_t *v = (vmidi_t*) vv;
	int fd = open( filename, O_RDONLY );
	if(!fd)
		return;

	char *buf = (char*) vj_calloc( 64000 );
	int done = 0;
	if (read( fd, buf, 64000 ) > 0 )
	{
		int len = strlen( buf );
		int j,k=0;

		char value[1024];
		veejay_memset(value,0,sizeof(value));
	
		for( j = 0; j < len; j ++ )
		{
			if( buf[j] == '\0' ) break;
			if( buf[j] == '\n' )
			{
				char key[32];
				char widget[100];
				char message[200];
				int extra = 0;
				if(sscanf( value, "%s %d %s %s", key, &extra, widget, message ) == 4 )
				{
					veejay_memset( value,0,sizeof(value));
					k = 0;
					dvims_t *d = (dvims_t*) vj_calloc(sizeof(dvims_t));
					d->extra = extra;
					d->widget = NULL;
					if( strncasecmp( widget, "none", 4 ) !=0 )
						d->widget = strdup(widget);
					d->msg = strdup(message);
					int error = vevo_property_set( v->vims, key, 1, VEVO_ATOM_TYPE_VOIDPTR, &d);
					veejay_memset( key,0,sizeof(key));
					veejay_memset( widget,0,sizeof(widget));
					veejay_memset( message,0,sizeof(message));
				}

			}
			if( buf[j] != '\n' && buf[j] != '\0' )
				value[k++] = buf[j];
		
		}
	
	}
}

void	vj_midi_save(void *vv, const char *filename)
{
	vmidi_t *v = (vmidi_t*) vv;
	int fd = open( filename, O_TRUNC|O_CREAT|O_WRONLY );
	if(!fd)
	{
		vj_msg(VEEJAY_MSG_ERROR, "Unable to save MIDI settings to %s",filename);
		return;
	}

	char **items = vevo_list_properties( v->vims );
	int i;
	if( items == NULL )
	{
		vj_msg(VEEJAY_MSG_ERROR, "No MIDI events learned yet");
		return;
	}
	uint32_t count = 0;
	for( i =0 ; items[i] != NULL ;i ++ )
	{
		char tmp[512];
		dvims_t *d  = NULL;
		if( vevo_property_get( v->vims, items[i], 0, &d ) == VEVO_NO_ERROR )
		{
			snprintf(tmp, 512, "%s %d %s %s\n",
				items[i],
				d->extra,
				(d->widget == NULL ? "none" : d->widget ),
				d->msg );
			write( fd, tmp, strlen( tmp ));
			count ++;
		}
	}

	close(fd);

	vj_msg(VEEJAY_MSG_INFO, "Wrote %d MIDI events to %s", count, filename );
}

void	vj_midi_learning_vims( void *vv, char *widget, char *msg, int extra )
{
	vmidi_t *v = (vmidi_t*) vv;
	if( !v->learn )
		return;
	dvims_t *d = (dvims_t*) vj_malloc(sizeof(dvims_t));
	d->extra = extra;	
	d->msg = (msg == NULL ? NULL : strdup(msg));
	d->widget = (widget == NULL ? NULL : strdup(widget));
	char key[32];
	snprintf(key,sizeof(key), "%03d%03d", v->learn_event[0],v->learn_event[1] );

	int error = vevo_property_set( v->vims, key, 1, VEVO_ATOM_TYPE_VOIDPTR, &d );
	if( error != VEVO_NO_ERROR )
		return;
	vj_msg( VEEJAY_MSG_INFO, 
		"Midi %x: %x ,%x learned", v->learn_event[0],v->learn_event[1],v->learn_event[2]);
}

void	vj_midi_learning_vims_simple( void *vv, char *widget, int id )
{
	char message[100];
	if( widget == NULL )
		snprintf(message,sizeof(message), "%03d:;", id );
	else
		snprintf(message,sizeof(message), "%03d:", id );
	vj_midi_learning_vims( vv, widget, message, (widget == NULL ? 0 : 1 ) );
}

void	vj_midi_learning_vims_spin( void *vv, char *widget, int id )
{
	char message[100];
	if( widget == NULL )
		snprintf(message,sizeof(message), "%03d:;", id );
	else
		snprintf(message,sizeof(message), "%03d:", id );
	vj_midi_learning_vims( vv, widget, message, (widget == NULL ? 0 : 2) );
}


void	vj_midi_learning_vims_complex( void *vv, char *widget, int id, int first , int extra)
{
	char message[100];
	snprintf( message, sizeof(message), "%03d:%d",id, first );

	vj_midi_learning_vims( vv, widget, message, extra );
}

void	vj_midi_learning_vims_msg( void *vv, char *widget, int id, int arg )
{
	char message[100];
	snprintf(message, sizeof(message), "%03d:%d;",id, arg );

	vj_midi_learning_vims( vv, widget, message, 0 );
}

void	vj_midi_learning_vims_msg2(void *vv, char *widget, int id, int arg, int b )
{
	char message[100];
	snprintf(message,sizeof(message), "%03d:%d %d;", id, arg,b );
	vj_midi_learning_vims( vv, widget, message, 0 );
}

void	vj_midi_learning_vims_fx( void *vv, int widget, int id, int a, int b, int c, int extra )
{
	char message[100];
	char wid[100];
	snprintf(message,sizeof(message), "%03d:%d %d %d", id, a,b,c );
	snprintf(wid, sizeof(wid),"slider_p%d", widget );
	vj_midi_learning_vims( vv, wid, message, extra );
}

static	void	queue_vims_event( vmidi_t *v, int *data )
{
	char key[32];
	if( v->learn )
	{
		veejay_memcpy( v->learn_event, data, sizeof(v->learn_event ));
		vj_msg(VEEJAY_MSG_INFO, "MIDI %x:%x,%x -> ?", v->learn_event[0],v->learn_event[1],
			v->learn_event[2]);
		return;
	}

	snprintf(key,sizeof(key), "%03d%03d", data[0],data[1] );

	dvims_t *d = NULL;
	int error = vevo_property_get( v->vims, key, 0, &d);
	if( error == VEVO_NO_ERROR )
	{
		if( d->extra )
		{	//@ argument is dynamic
			double min = 0.0;
			double max = 0.0;
			double tmp = 0.0;
			double val = 0.0;
			switch(d->extra)
			{
				case 1: //slider
				{
					GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE(
							glade_xml_get_widget_( v->mw, d->widget ) ) );
					min = a->lower;
					max = a->upper;
				}
				break;
				case 2: //spinbox
					gtk_spin_button_get_range( GTK_SPIN_BUTTON(
							glade_xml_get_widget_( v->mw, d->widget)), &min, &max);
				
				break;
			}

			if( data[0] == SND_SEQ_EVENT_PITCHBEND )
			{
				tmp = 16384.0 / max;
				val = data[2] + 8192;
				if( val > 0 )
					val = val / tmp;
			}
			else if( data[0] == SND_SEQ_EVENT_CONTROLLER || data[0] == SND_SEQ_EVENT_KEYPRESS )
			{
				tmp = 127.0 / max;
				val = data[2];
				if( val > 0 )
					val = val / tmp;
			} else {
				return;
			}

			char vims_msg[255];
			snprintf(vims_msg,sizeof(vims_msg), "%s %d;", d->msg, (int) val );
			msg_vims( vims_msg );
			vj_msg(VEEJAY_MSG_INFO, "MIDI %x:%x, %x ->  vims %s", data[0], data[1],data[2], vims_msg);
		}
		else
		{
			msg_vims( d->msg );
			vj_msg(VEEJAY_MSG_INFO, "MIDI %x: %x,%x -> vims %s", data[0],data[1],data[2], d->msg);
		}
	}
	else
	{
		vj_msg(VEEJAY_MSG_ERROR, "No vims event for MIDI %x:%x,%x found",
				data[0],data[1],data[2]);
	}
}

static	void	vj_midi_action( vmidi_t *v )
{
	snd_seq_event_t *ev;
	int data[4] = { 0,0,0,0};
	snd_seq_event_input( v->sequencer, &ev );

	data[0] = ev->type;

	switch( ev->type )
	{
		/* controller: channel <0-N>, <modwheel 0-127> */
		case SND_SEQ_EVENT_CONTROLLER:
			data[1] = ev->data.control.channel;
			data[2] = ev->data.control.value;
		break;
		case SND_SEQ_EVENT_PITCHBEND:
			data[1] = ev->data.control.channel;
			data[2] = ev->data.control.value;
		break;
		case SND_SEQ_EVENT_NOTEON:
			data[2] = ev->data.control.channel;
			data[1] = ev->data.note.note;
		break;
		case SND_SEQ_EVENT_NOTEOFF:
			data[2] = ev->data.control.channel;
			data[1] = ev->data.note.note;
		break;
		case SND_SEQ_EVENT_KEYPRESS:
			data[1] = ev->data.control.channel;
			data[2] = ev->data.note.velocity;
		break;
		default:
			break;
	}

	queue_vims_event( v, data );

	snd_seq_free_event( ev );
}

int	vj_midi_handle_events(void *vv)
{
	vmidi_t *v = (vmidi_t*) vv;
	if( poll( v->pfd, v->npfd, 0 ) > 0 )
	{
	//@ snd_event_input_pending doesnt work
	//	while( snd_seq_event_input_pending( v->sequencer, 0 ) > 0 )
			vj_midi_action( v );
		return 1;
	}
	return 0;
}


void	*vj_midi_new(void *mw)
{
	vmidi_t *v = (vmidi_t*) vj_calloc(sizeof(vmidi_t));
	int portid = 0;

	if( snd_seq_open( &(v->sequencer), "hw", SND_SEQ_OPEN_DUPLEX, 0 ) < 0 )
	{
		veejay_msg(0, "Error opening ALSA sequencer");
		if(v) free(v);
		return NULL;
	}

	snd_seq_set_client_name( v->sequencer, "Veejay" );

	if( (portid = snd_seq_create_simple_port( v->sequencer, "Veejay",
			SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE ,
			SND_SEQ_PORT_TYPE_APPLICATION )) < 0 )
	{
		veejay_msg(0, "Error creating sequencer port");
		if(v) free(v);
		return NULL;
	}

	v->npfd = snd_seq_poll_descriptors_count( v->sequencer, POLLIN );
	if( v->npfd <= 0 )
	{
		veejay_msg(0,"Unable to poll in from sequencer");
		if(v) free(v);
		return NULL;
	}
	v->pfd     = (struct pollfd *) vj_calloc( v->npfd * sizeof( struct pollfd ));
	v->mw      = mw;
	v->learn   = 0;
	v->vims    = vpn(VEVO_ANONYMOUS_PORT);	

	snd_seq_poll_descriptors( v->sequencer, v->pfd, v->npfd, POLLIN );

	veejay_msg(VEEJAY_MSG_INFO, "MIDI listener active");

	return (void*) v;
}


