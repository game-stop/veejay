#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#include <veejay/vj-splitdisplay.h>
#include <libstream/vj-tag.h>
#include <veejay/vj-lib.h>

#define MAX_REGIONS 	8

typedef struct
{
	int sample_id;
	int	active;
} split_sample_t;

typedef struct
{
	int width;
	int height;
	int x;
	int y;
} split_geom_t;

typedef struct
{
	split_sample_t **samples;
	int	width;
	int	height;
	int num_screens;
	uint8_t *area;
	uint8_t *darea[3]; //@ temporary draw/queue buffer
	uint8_t *ptr[3];
	char    layout[ 1+ MAX_REGIONS ];
} split_display_t;


static	void	vj_split_change_screens(split_display_t *sd, int n)
{
	int k;
	if( sd->samples && n != sd->num_screens ) {
		for( k = 0; k < sd->num_screens; k ++ )
		{
			if(sd->samples[k])
				free(sd->samples[k]);
		}
		free(sd->samples);
	}

	sd->num_screens = n;
	sd->samples = (split_sample_t**) (vj_calloc(sizeof(split_sample_t*) * n ));
	for( k = 0; k < sd->num_screens; k ++ ) {
		sd->samples[k] = (split_sample_t*) vj_calloc(sizeof(split_sample_t));
	}

	int auto_n = vj_tag_size()-1;
	if( (auto_n - sd->num_screens ) < 0 )
	{
		veejay_msg(0, "Stream limit is %d! Create more tags first", auto_n );
		return;
	}

	sd->samples[0]->sample_id = 2;
	sd->samples[1]->sample_id = 4;
	sd->samples[2]->sample_id = 6;

	for( k = 0; k < sd->num_screens; k ++ ) {
	//	sd->samples[k]->sample_id = ( auto_n - sd->num_screens ) + k; 
		veejay_msg(VEEJAY_MSG_DEBUG , "Split #%d = %d", k, sd->samples[k]->sample_id );
	}
}

void	vj_split_destroy( void *s )
{

}

void	*vj_split_display(int w, int h)
{
	split_display_t *sd = (split_display_t*) vj_calloc( sizeof( split_display_t));
	sd->area 			= (uint8_t*) vj_calloc(sizeof(uint8_t) * w * h * 6 );
	sd->width = w;
	sd->height = h;
 	sd->darea[0]			= sd->area + (w*h*3);
	sd->darea[1]			= sd->darea[0] + (w*h);
	sd->darea[2]			= sd->darea[1] + ( (w*h)>>1);
	vj_split_change_screens(sd, 3);

	return sd;
}

void	vj_split_get_frame( void *sd, uint8_t *row[3] )
{
	split_display_t *sdt 	= (split_display_t*)sd;
	if( sdt->ptr[0] == NULL ) {
		row[0] = sdt->area;
   		row[1] = sdt->area + (sdt->width * sdt->height);
   		row[2] = row[1] + ( (sdt->width*sdt->height)>>1);	
	} else {
		row[0] = sdt->ptr[0];
		row[1] = sdt->ptr[1];
		row[2] = sdt->ptr[2];
	}
}

static	void vj_split_clear_region ( split_display_t *sdt , int position )
{
	uint8_t *plane_y  = sdt->area;
	uint8_t *plane_u  = plane_y   + (sdt->width * sdt->height);
	uint8_t *plane_v  = plane_u   + ((sdt->width * sdt->height)>>1);

	int off_x  = ( sdt->width / sdt->num_screens ) * position;
	int stop_x = ( sdt->width / sdt->num_screens ) + off_x;
	int w = sdt->width;
	int h = sdt->height;	
	int x,y;
	uint8_t black = get_pixel_range_min_Y();
/*
	for( y = 0; y < h; y ++ ) {
		for( x = off_x; x < stop_x; x ++ ) {
			plane_y[ y * w + x] = black;
		}
	}

	off_x = off_x;
	off_x = stop_x;
	
	for( y = 0; y < h ; y ++ ) {
		for( x = off_x; x < stop_x ; x ++ ) {
			plane_u[ y * w + x ] = 128;
			plane_v[ y * w + x ] = 128;
		}
	}*/
	veejay_msg(VEEJAY_MSG_DEBUG, "clear split #%d (%d -> %d)", position, (sdt->width*sdt->height),
		   		((sdt->width*sdt->height)>>1)	);
}

static void	vj_split_push_frame( split_display_t *sdt, uint8_t *data[3], int position )
{
	uint8_t *plane_y = sdt->ptr[0];
	uint8_t *plane_u = sdt->ptr[1];
	uint8_t *plane_v = sdt->ptr[2];

	if( sdt->ptr[0] == NULL ) {
		plane_y  = sdt->area;
		plane_u  = plane_y   + (sdt->width * sdt->height);
		plane_v  = plane_u   + ((sdt->width * sdt->height)>>1);
	}

	int off_x  = ( sdt->width / sdt->num_screens ) * position;
	int stop_x = ( sdt->width / sdt->num_screens ) + off_x;
	int w = sdt->width;
	int h = sdt->height;	
	int x,y;

	for( y = 0; y < h; y ++ ) {
		for( x = off_x; x < stop_x; x ++ ) {
			plane_y[ y * w + x] = data[0][ y * w + x ];
		}
	}

	off_x = off_x >> 1;
	off_x = stop_x >> 1;
	
	for( y = 0; y < h ; y ++ ) {
		for( x = off_x; x < stop_x ; x ++ ) {
			plane_u[ y * w + x ] = data[1][y * w + x];
			plane_v[ y * w + x ] = data[2][y * w + x];
		}
	}
	veejay_msg(VEEJAY_MSG_DEBUG, "push split #%d", position );

}

/*
 [00000000000000000000000000000000] 0
 [10000000000000000000000000000000] 1
 [01000000000000000000000000000000] 2
 [11000000000000000000000000000000] 3
 [00100000000000000000000000000000] 4
 [10100000000000000000000000000000] 5
 [01100000000000000000000000000000] 6
 [11100000000000000000000000000000] 7

 ...

*/

void	vj_split_set_stream_in_screen( void *sd, int stream_id, int screen_no )
{
	split_display_t *sdt = (split_display_t*) sd;
	int n = sdt->num_screens;
	if( screen_no < 1 ) screen_no = 1; else if ( screen_no > 7 ) screen_no = 7;
	if( sdt->num_screens <= screen_no ) {
		n = screen_no + 1;
		if( n > 7 )
		{
			n = 7;
			return;
		}
	}
	vj_split_change_screens( sdt, n );
  	sdt->samples[ screen_no ]->sample_id = stream_id;
}

int		vj_split_get_num_screens( void *sd )
{
	split_display_t *sdt = (split_display_t*) sd;
  	return sdt->num_screens;
}


static int testing_interval = 0;
static int testing_value = 0;
void	vj_split_change_screen_setup(void *sd, int value)
{
	split_display_t *sdt = (split_display_t*) sd;
  	char str[ 1 + (8 * sizeof(int)) ];
	int i;

	//@ one to rule them all
	testing_interval ++;
	if( testing_interval > 100 )
	{
		testing_value ++ ;
		if( testing_value > 7 )
			testing_value = 0;
		testing_interval = 0;
	}
	veejay_msg(0, "%d / %d __ %d", testing_interval, 100, testing_value );

	int my_precious = testing_value;
	
	memset( str, 0,1 + (8*sizeof(int)));
	for ( i = 0; i < (8*sizeof(int)); i ++ ) {
		str[i] = ( my_precious & 1 ) + '0';
		my_precious      = my_precious >> 1;
	}

	for( i = 0; i < sdt->num_screens; i ++ ) {
		sdt->samples[i]->active = ( str[i] == '1' ? 1 : 0);
	}

	memset( sdt->layout,0,sizeof(char)*MAX_REGIONS);
	for( i = 0; i < sdt->num_screens;i ++ )
	{
		sdt->layout[i] = str[i];
		if( sdt->layout[i] == '1' ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "Split #%d = %d, (active=%d)",
					i, sdt->samples[i]->sample_id, sdt->samples[i]->active );
		} else {
			veejay_msg(VEEJAY_MSG_DEBUG, "Split %d = off (sample %d)",
					i, sdt->samples[i]->sample_id );
		}
	}

	veejay_msg(VEEJAY_MSG_INFO, "Changed screen setup to %d/ %s",my_precious,sdt->layout );


}

void	vj_split_get_layout( void *sd, char *dst )
{
	split_display_t *sdt = (split_display_t*) sd;
	strncpy(dst, sdt->layout, 8 );
}

int		*vj_split_get_samples( void *sd )
{
	split_display_t *sdt = (split_display_t*) sd;
	int *res = (int*) vj_malloc(sizeof(int)* MAX_REGIONS);
	int i;
	for( i = 0; i <MAX_REGIONS; i ++ ) {
		res[i] = sdt->samples[i]->sample_id;
	}
	return res;
}

void	vj_split_change_num_screens( void *sd, int n_screens )
{
	if( n_screens < 1 || n_screens > 8 )
		return;

	split_display_t *sdt = (split_display_t*) sd;

	vj_split_change_screens( sdt, n_screens );
}


void	vj_split_process_frame( void *sd , uint8_t *dst[3])
{
	split_display_t *sdt 	= (split_display_t*)sd;

	sdt->ptr[0] = dst[0];
	sdt->ptr[1] = dst[1];
	sdt->ptr[2] = dst[2];

	int k;
	for	( k = 0; k < sdt->num_screens ; k ++ )
	{
		if( sdt->samples[k]->sample_id == 0 ) {
			vj_split_clear_region( sdt, k );
			continue;
		}

		if( sdt->samples[k]->active == 0 ) {
			vj_split_clear_region( sdt, k );
			continue;
		}
					
		//@ streams only 
		if(! vj_tag_get_frame( sdt->samples[k]->sample_id, sdt->darea, NULL ) )
			continue;

		vj_split_push_frame( sdt, sdt->darea, k );
	}

}

