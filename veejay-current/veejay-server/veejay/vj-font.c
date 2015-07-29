/*
 * drawtext.c: print text over the screen
 * heavily modified to serve as font renderer in veejay
 ******************************************************************************
 * Author: Gustavo Sverzut Barbieri <gsbarbieri@yahoo.com.br>
 *         Niels Elburg <nwelburg@gmail.com> (Nov. 2006-2008)
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libvje/vje.h>
#include <libvje/effects/common.h>
#include <libsubsample/subsample.h>
#include <libvevo/libvevo.h>
#include <mjpegtools/mpegconsts.h>
#include <mjpegtools/mpegtimecode.h>
#include <libvjmem/vjmem.h>
#include <pthread.h>
#include <veejay/vj-lib.h>
#ifdef HAVE_XML2
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#endif

extern	int	vj_tag_size();

#ifdef HAVE_FREETYPE
#include <fcntl.h>
#include <ft2build.h>
#include FT_FTSNAMES_H
#include FT_TTNAMEID_H
#include FT_FREETYPE_H
#include FT_GLYPH_H

#define BSIZE 256

typedef struct
{	
	int id;
	char *text;
	long  start;
	long  end;
	unsigned int x;
	unsigned int y;
	int size;
	int font;
	char *key;
	int time;   
	uint8_t bg[3];
	uint8_t fg[3];
	uint8_t ln[3];
	uint8_t alpha[3];
	int use_bg;
	int outline;
} srt_seq_t;


typedef struct
{
	int	id[16];
} srt_cycle_t;

typedef struct {
	unsigned char *text;
	unsigned char *font;
	uint8_t	*fd_buf;
	unsigned int w;
	unsigned int h;
	unsigned int x;
	unsigned int y;
	int bg;
	int outline;
	uint8_t alpha[3];

	uint8_t bgcolor[3];
	uint8_t fgcolor[3];
	uint8_t lncolor[3];
	
	FT_Library library;
	FT_Face    face;
	FT_Glyph   glyphs[ BSIZE ]; 
	FT_Bitmap  bitmaps[ BSIZE ];
	int        advance[ BSIZE ];
	int        bitmap_left[ BSIZE ];
	int        bitmap_top[ BSIZE ];
	unsigned int glyphs_index[ BSIZE ];
	int        text_height;
	int	   text_width;
	int	   current_font;
	int        baseline;
	int use_kerning;
	int current_size;
	unsigned char	**font_table;
	unsigned char    **font_list;
	int	auto_number;
	int	font_index;
	long		index_len;
	srt_seq_t	**index;
	float		fps;
	void	*dictionary;
	void	*plain;
	char	*add;
	char 	*prev;
	pthread_mutex_t	mutex;
//	srt_cycle_t	*text_buffer;
//	uint32_t	 text_max_size;
} vj_font_t;

static int	configure(vj_font_t *f, int size, int font);
static char *make_key(int id);
static char *vj_font_pos_to_timecode( vj_font_t *font, long pos );
static long	vj_font_timecode_to_pos( vj_font_t *font, const char *tc );
static srt_seq_t	*vj_font_new_srt_sequence(vj_font_t *font, int id,unsigned char *text, long lo, long hi );
static void	vj_font_del_srt_sequence( vj_font_t *f, int seq_id );
static void	vj_font_store_srt_sequence( vj_font_t *f, srt_seq_t *s );
static int find_fonts(vj_font_t *ec, char *path);
static unsigned char *select_font( vj_font_t *ec, int id );
static void vj_font_substract_timecodes( vj_font_t *font, unsigned char *tc_srt, long *lo, long *hi );
static unsigned char *vj_font_split_strd( unsigned char *str );
static unsigned char *vj_font_split_str( unsigned char *str );
static	unsigned char *get_font_name( vj_font_t *f,unsigned char *font, int id );
static	int	get_default_font( vj_font_t *f );

static  char	selected_default_font[1024];

static	void	font_lock(vj_font_t *f)
{
	pthread_mutex_lock( &(f->mutex) );
}
static  void	font_unlock( vj_font_t *f )
{
	pthread_mutex_unlock( &(f->mutex) );
}

static char *make_key(int id)
{
	char key[32];
	snprintf(key,sizeof(key),"s%d",id);
	return vj_strdup(key);
}


int	vj_font_prepare( void *font, long s1, long s2 )
{
	vj_font_t *f = (vj_font_t*) font;
	char **list = vevo_list_properties( f->dictionary );
	int i;
	int N = 0;

	if(f->index)
		free(f->index);
	f->index = NULL;
	f->index_len = 0;
	
	if( list == NULL )
		return 0;

	for( i = 0; list[i] != NULL ; i ++ ) 
		N++;
/*
	long min = s1;
	long max = s2;
	for( i = 0; list[i] != NULL ; i ++ ) {
		void *srt = NULL;
		int error = vevo_property_get( f->dictionary, list[i], 0, &srt );
		if( error != VEVO_NO_ERROR )
			continue;

		srt_seq_t *s = (srt_seq_t*) srt;
		if( s->start < min )
			min = start;
		if( s->end > max )
			max = s->end;
	}

	s1 = min;
	s2 = max;*/


	if( N == 0 ) 	
		return 0;

	f->index = (srt_seq_t**) vj_calloc(sizeof(srt_seq_t*) * N );
	f->index_len = N;
	for( i = 0; list[i] != NULL ; i ++ ) {
		void *srt = NULL;
		int error = vevo_property_get( f->dictionary, list[i], 0, &srt );
		if( error == VEVO_NO_ERROR )
		{
			srt_seq_t *s = (srt_seq_t*) srt;
			if( s->start > s1 )
				s->start = s1;
			if( s->end > s2 )
				s->end = s2;
			f->index[i] = s;
		} else {
			f->index[i] = NULL;
		}
		free(list[i]);
	}

	free(list);
	return N;
}

int	vj_font_srt_sequence_exists( void *font, int id )
{
	if(!font)
		return 0;
	vj_font_t *f = (vj_font_t*) font;
	if(!f->dictionary )
		return 0;

	char *key = make_key(id);
	void *srt = NULL;
	int error = vevo_property_get( f->dictionary, key,0,&srt );
	free(key);
	if( error == VEVO_NO_ERROR )
		return 1;
	return 0;
}

static	char 	*vj_font_pos_to_timecode( vj_font_t *font, long pos )
{
	vj_font_t *ff = (vj_font_t*) font;
	MPEG_timecode_t tc;
        veejay_memset(&tc, 0,sizeof(MPEG_timecode_t));
       	char tmp[32];
	
        y4m_ratio_t ratio = mpeg_conform_framerate( ff->fps );
        int n = mpeg_framerate_code( ratio );

        mpeg_timecode(&tc, pos, n, ff->fps );

        snprintf(tmp, sizeof(tmp), "%2d:%2.2d:%2.2d:%2.2d",
                tc.h, tc.m, tc.s, tc.f );

	return vj_strdup(tmp);
}

static	long	vj_font_timecode_to_pos( vj_font_t *font, const char *tc )
{
	int t[4];

	sscanf( tc, "%1d:%02d:%02d:%02d", &t[0],&t[1],&t[2],&t[3] );

	long res = 0;

	res =  (long) t[3];
	res += (long) t[2] * font->fps;
	res += (long) t[1] * font->fps * 60.0;
	res += (long) t[0] * font->fps * 3600.0;

	return res;
}

static	void	vj_font_substract_timecodes( vj_font_t *font, unsigned char *tc_srt, long *lo, long *hi )
{
	char tc1[20] = { 0 };
	char tc2[20] = { 0 };

	sscanf( (char*) tc_srt, "%s %*s %s", tc1,tc2 );

	*lo = vj_font_timecode_to_pos( font, tc1 );
	*hi = vj_font_timecode_to_pos( font, tc2 );
}

static unsigned char	*vj_font_split_strd( unsigned char *str )
{
	const unsigned char *p = str;
	unsigned char  *res    = NULL;
	int   i = 0;
	while( *p != '\0' && *p != '\n' )
	{
		*p ++;
		i++;
	}
	if(*p == '\n')
	{
		*p ++;
		i ++;
	}
	if(*p == '\n' )
	{
		*p ++;
		i ++;
	}	
	else
	{
		return NULL;
	}
	
	if( i <= 0 )
		return NULL;
	
	res = (unsigned char*) strndup( (char*)str, i );
	return res;
}

static unsigned char	*vj_font_split_str( unsigned char *str )
{
	const unsigned char *p = str;
	unsigned char  *res    = NULL;
	int   i = 0;
	while( *p != '\0' && *p != '\n' )
	{
		*p ++;
		i++;
	}
	if(*p == '\n')
	{
		*p ++;
		i ++;
	}
	if( i <= 0 )
		return NULL;
	
	res = (unsigned char*) strndup( (char*)str, i );
	return res;
}

static srt_seq_t 	*vj_font_new_srt_sequence( vj_font_t *f,int id,unsigned char *text, long lo, long hi )
{
	char tmp_key[16];
	srt_seq_t *s = (srt_seq_t*) vj_calloc(sizeof( srt_seq_t ));
	s->id   = id;
	s->text = vj_strdup( (const char*)text );
	s->start   = lo;
	s->end   = hi;
	sprintf(tmp_key, "s%d", id );
	s->bg[0] = 0;
	s->bg[1] = 0;
	s->bg[2] = 0;
	s->fg[0] = 255;
	s->fg[1] = 255;
	s->fg[2] = 255;
	s->ln[0] = 200;
	s->ln[1] = 255;
	s->ln[2] = 255;
	s->alpha[0] = 0;
	s->alpha[1] = 0;
	s->alpha[2] = 0;
	s->use_bg = 0;
	s->outline = 0;
	s->size  = 40;
	s->font  = get_default_font(f);
	s->key  = vj_strdup(tmp_key);

	veejay_msg(VEEJAY_MSG_DEBUG,
			"New SRT sequence: '%s' starts at position %ld , ends at position %ld",
				text,lo,hi );

	return s;
}

void			vj_font_set_current( void *font , int cur )
{
	vj_font_t *f = (vj_font_t*) font;
	if(vj_font_srt_sequence_exists( font, cur ))
	{
		f->auto_number = cur;
	}

}

static	void		vj_font_del_srt_sequence( vj_font_t *f, int seq_id )
{
	if(seq_id == 0 )
		seq_id = f->auto_number;
	
	char *key = make_key(seq_id );
	void *srt = NULL;

	
	int error = vevo_property_get( f->dictionary, key, 0, &srt ) ;
	if( error == VEVO_NO_ERROR )
	{
		srt_seq_t *s = (srt_seq_t*) srt;
		free(s->text);
		free(s->key);
		free(s);
		vevo_property_set( f->dictionary, key, VEVO_ATOM_TYPE_VOIDPTR, 0,NULL );

	}
	free(key);
}

static void		vj_font_store_srt_sequence( vj_font_t *f, srt_seq_t *s )
{
	void *srt = NULL;
	int error = vevo_property_get( f->dictionary, s->key, 0, &srt );
	if( error == VEVO_NO_ERROR )
	{
		srt_seq_t *old = (srt_seq_t*) srt;

		veejay_msg(VEEJAY_MSG_DEBUG, "replacing subtitle %d, '%s', %ld -> %ld",
				old->id, old->text,old->start,old->end );
		free(old->text);
		free(old->key);
		free(old);
	}

	error = vevo_property_set( f->dictionary, s->key, VEVO_ATOM_TYPE_VOIDPTR, 1,&s );
	if( error != VEVO_NO_ERROR )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to store SRT sequence '%s'", s->key );
	}
}

int	vj_font_load_srt( void *font, const char *filename )
{
	vj_font_t *ff = (vj_font_t*) font;
	FILE *f = fopen( filename, "r" );
	unsigned int len = 0;
	if(!f)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to load SRT file '%s'",filename );
		return 0;
	}
	fseek( f, 0, SEEK_END );
	len = ftell( f );

	if( len <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "SRT file '%s' is empty", filename );
		return 0;
	}
	rewind( f );

	ff->fd_buf = (uint8_t*) vj_calloc( len );
	fread( ff->fd_buf, len,1, f );

	fclose( f );

	//parse the file
	unsigned char *str = ff->fd_buf;
	int offset   = 0;


	font_lock( ff );
	while( offset < len )
	{	
		unsigned char *line = vj_font_split_str( str );
		if(!line)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to parse sequence ID in srt file");
			font_unlock( ff );
			return 0;
		}
		int   n  = strlen( (char*)line );

		offset += n;
		str += n;
		
		unsigned char *timecode = vj_font_split_str( str );
		if(!timecode)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to parse timecode in srt file");
			font_unlock(ff);
			return 0;
		}
		n = strlen( (char*)timecode );

		offset += n;
		str +=  n;

		unsigned char *text = vj_font_split_strd ( str );
		if(!text)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to parse subtitle text in srt file");
			font_unlock(ff);
			return 0;
		}
		n = strlen ( (char*) text );

		offset += n;
		str += n;

		long lo=0,hi=0;
		int seq_id = atoi( (char*)line );

		vj_font_substract_timecodes( ff, timecode, &lo, &hi );

		if( lo == hi )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "It makes no sense to create a subtitle sequence with length 0");
			font_unlock(ff);
			return 0;
		}

		srt_seq_t *s = vj_font_new_srt_sequence(ff, seq_id, text, lo,hi );
	
		vj_font_store_srt_sequence( ff, s );

		free(line);
		free(timecode);
		free(text);
	}		
	font_unlock( ff );
	
	free( ff->fd_buf );
	
	return 1;
}
static   int    get_xml_int( xmlDocPtr doc, xmlNodePtr node )
{
        xmlChar *tmp = xmlNodeListGetString( doc, node->xmlChildrenNode, 1 );
        char *ch = UTF8toLAT1( tmp );
        int res = 0;
        if( ch )
        {
                res = atoi( ch );
                free(ch);
        }
        if(tmp)
                free(tmp);
        return res;
}

static   void    get_xml_3int( xmlDocPtr doc, xmlNodePtr node, int *first , int *second, int *third )
{
        xmlChar *tmp = xmlNodeListGetString( doc, node->xmlChildrenNode, 1 );
        char *ch = UTF8toLAT1( tmp );
        if( ch )
        {
                sscanf( ch, "%d %d %d" , first, second, third );
                free(ch);
        }
        if(tmp)
                free(tmp);
}


void	vj_font_xml_unpack( xmlDocPtr doc, xmlNodePtr node, void *font )
{
	if(!node)
		return;
	vj_font_t *f = (vj_font_t*) font;

	int x=0,y=0,id=0,size=0,type=0, use_bg=0, outline=0;
	int bg[3] = {0,0,0};
	int fg[3] = {0,0,0};
	int alpha[3] = {0,0,0};
	int ln[3] = {0,0,0};

	while( node != NULL )
	{
		if( !xmlStrcmp( node->name, (const xmlChar*) "srt_id" ))
			id = get_xml_int( doc, node );	
		if( !xmlStrcmp( node->name, (const xmlChar*) "x_pos" ))
			x = get_xml_int( doc,node );
		if( !xmlStrcmp( node->name, (const xmlChar*) "y_pos" ))
			y = get_xml_int( doc, node );
		if( !xmlStrcmp( node->name, (const xmlChar*) "font_size" ))
			size = get_xml_int(doc,node);
		if( !xmlStrcmp( node->name, (const xmlChar*) "font_family" ))
			type = get_xml_int( doc, node );
		if( !xmlStrcmp( node->name, (const xmlChar*) "bg" ))
			get_xml_3int( doc, node, &bg[0], &bg[1], &bg[2] );
		if( !xmlStrcmp( node->name, (const xmlChar*) "fg" ))
			get_xml_3int( doc, node, &fg[0], &fg[1], &fg[2] );
		if( !xmlStrcmp( node->name, (const xmlChar*) "ln" ))
			get_xml_3int( doc, node, &ln[0], &ln[1], &ln[2] );
		if( !xmlStrcmp( node->name, (const xmlChar*) "alpha" ))
			get_xml_3int( doc, node, &alpha[0], &alpha[1], &alpha[2] );	
		if( !xmlStrcmp( node->name, (const xmlChar*) "use_bg" ))
			use_bg = get_xml_int( doc, node );
		if( !xmlStrcmp( node->name, (const xmlChar*) "use_outline" ))
			outline = get_xml_int(doc,node);

		node = node->next;
	}

	char *key = make_key( id );
	srt_seq_t *s = NULL;
	if( vevo_property_get( f->dictionary, key, 0, &s ) == VEVO_NO_ERROR )
	{
		s->x = x; s->y = y; s->size = size; s->font = type;
		s->use_bg = use_bg; s->outline = outline;
		s->bg[0] = bg[0]; s->bg[1] = bg[1]; s->bg[2] = bg[2];
		s->fg[0] = fg[0]; s->fg[1] = fg[1]; s->fg[2] = fg[2];
		s->ln[0] = ln[0]; s->ln[1] = ln[1]; s->ln[2] = ln[2];
		s->alpha[0] = alpha[0];
		s->alpha[1] = alpha[1];
		s->alpha[2] = alpha[2];
	}
	else
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Sequence %d (%s) not in .srt file (tried dictionary %p)", id,key, f->dictionary );
	}
	free(key);

}

void	vj_font_xml_pack( xmlNodePtr node, void *font )
{
	vj_font_t *ff = (vj_font_t*) font;
        if(ff == NULL )
		return;

	char **items = vevo_list_properties ( ff->dictionary );
        if(!items)
                return;

	char buf[100];
	int i;
        for( i = 0; items[i] != NULL ; i ++ )
        {
                void *srt = NULL;
                if ( vevo_property_get( ff->dictionary, items[i], 0, &srt ) == VEVO_NO_ERROR )
                {
                        srt_seq_t *s = (srt_seq_t*) srt;
		
			xmlNodePtr childnode = xmlNewChild( node, NULL, (const xmlChar*) "SUBTITLES" , NULL );
	
				sprintf(buf, "%d", s->id );
				xmlNewChild(childnode, NULL, (const xmlChar*) "srt_id", (const xmlChar*) buf );	

				sprintf(buf, "%d",s->x );
				xmlNewChild(childnode, NULL, (const xmlChar*) "x_pos", (const xmlChar*) buf );

				sprintf(buf, "%d", s->y );
				xmlNewChild(childnode, NULL, (const xmlChar*) "y_pos", (const xmlChar*) buf );
	
				sprintf(buf, "%d", s->size );
				xmlNewChild(childnode, NULL, (const xmlChar*) "font_size", (const xmlChar*) buf );

				sprintf(buf, "%d", s->font );
				xmlNewChild(childnode, NULL, (const xmlChar*) "font_family", (const xmlChar*) buf );
	
				sprintf(buf, "%d %d %d", s->bg[0],s->bg[1],s->bg[2] );
				xmlNewChild(childnode, NULL, (const xmlChar*) "bg" , (const xmlChar*) buf );

				sprintf(buf, "%d %d %d", s->fg[0], s->fg[1], s->fg[2] );	
				xmlNewChild(childnode, NULL, (const xmlChar*) "fg", (const xmlChar*) buf );

				sprintf(buf, "%d %d %d", s->ln[0], s->ln[1], s->ln[2] );
				xmlNewChild(childnode,NULL, (const xmlChar*) "ln", (const xmlChar*) buf );

				sprintf(buf, "%d %d %d", s->alpha[0], s->alpha[1],s->alpha[2] );
				xmlNewChild(childnode, NULL, (const xmlChar*) "alpha", (const xmlChar*) buf );

				sprintf(buf, "%d", s->use_bg );
				xmlNewChild(childnode, NULL, (const xmlChar*) "use_bg", (const xmlChar*) buf );
	
				sprintf(buf, "%d", s->outline );
				xmlNewChild(childnode, NULL, (const xmlChar*) "use_outline", (const xmlChar*) buf );
					

                }
                free(items[i]);
        }
        free(items);
}


int     vj_font_save_srt( void *font , const char *filename )
{
	vj_font_t *ff = (vj_font_t*) font;
	if( ff == NULL )
		return 0;

	char **items = vevo_list_properties ( ff->dictionary );
	if(!items)
	{
		veejay_msg(0, "No subtitle sequences present, nothing to save");
		return 0;
	}
	int i;
	FILE *f = fopen( filename , "w" );
	if(!f)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot open file '%s' for writing", filename );
		return 0;
	}
	for( i = 0; items[i] != NULL ; i ++ )
	{
		void *srt = NULL;
		if ( vevo_property_get( ff->dictionary, items[i], 0, &srt ) == VEVO_NO_ERROR )
		{
			srt_seq_t *s = (srt_seq_t*) srt;
			int n = strlen(s->text);
			if( n > 0 )
			{
				char *tc1 = vj_font_pos_to_timecode( ff, s->start );
				char *tc2 = vj_font_pos_to_timecode( ff, s->end );
				fprintf( f, "%d\n%s --> %s\n%s\n\n",
					s->id,
					tc1,
					tc2,
					s->text );
				free(tc1);
				free(tc2);
			}
		}
		free(items[i]);
	}
	free(items);
	fclose( f );

	veejay_msg(VEEJAY_MSG_DEBUG, "Saved %d subtitles to %s", i, filename );

	return 1;
}

char    *vj_font_get_sequence( void *font, int seq )
{
	vj_font_t *ff = (vj_font_t*) font;
	char tmp[1024];
	srt_seq_t *s = NULL;
	void *srt = NULL;

	if( seq == 0 )
		seq = ff->auto_number;
	char *key = make_key( seq );	

	if( vevo_property_get( ff->dictionary, key, 0, &srt ) != VEVO_NO_ERROR )
		return NULL;
	
	s = (srt_seq_t*) srt;
	
	int tcl1, tcl2;
	char *tc1 = vj_font_pos_to_timecode( ff, s->start );
	char *tc2 = vj_font_pos_to_timecode( ff, s->end );
	int len = strlen(s->text);
	tcl1 = strlen(tc1);
	tcl2 = strlen(tc2);

	snprintf( tmp,sizeof(tmp), "%05d%09d%09d%02d%s%02d%s%03d%s%04d%04d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d",
			s->id,
			(int)s->start,
			(int)s->end,
			tcl1,
			tc1,
			tcl2,
			tc2,
			len,
			s->text,
	      		s->x,
	      		s->y,
	      	        s->font,
	      		s->size,
	      		s->bg[0],
	     		s->bg[1],
	      		s->bg[2],
	      		s->fg[0],
	      		s->fg[1],
	      		s->fg[2],
	      		s->use_bg,
	      		s->outline,
	      		s->ln[0],
	      		s->ln[1],
		        s->ln[2],
			s->alpha[0],
			s->alpha[1],
			s->alpha[2]);

	free(tc1);
	free(tc2);
	free(key);	
	return vj_strdup(tmp);
}

int	vj_font_new_text( void *font, unsigned char *text, long lo,long hi, int seq)
{
	vj_font_t *ff = (vj_font_t*) font;
	if( lo == hi )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "It makes no sense to make a subtitle of length 0" );
		return 0;
	}
	
	if(seq == 0 )
	{
		int an = ff->auto_number;
		while( vj_font_srt_sequence_exists( font, an ) )
			an++;
		ff->auto_number = an;
		veejay_msg(VEEJAY_MSG_DEBUG, "New subtitle sequence %d", an );
		seq = ff->auto_number;
	}

	font_lock( ff );
	
	srt_seq_t *s = vj_font_new_srt_sequence(ff, seq, text, lo,hi );
		
	vj_font_store_srt_sequence( ff, s );

	font_unlock(ff);

	return seq;
}
void    vj_font_del_text( void *font, int seq )
{
	vj_font_t *ff = (vj_font_t*) font;
	ff->auto_number = seq;
	font_lock( ff );
	vj_font_del_srt_sequence( ff, seq);
	font_unlock( ff );
}

void    vj_font_set_position( void *font, int x, int y )
{
	vj_font_t *ff = (vj_font_t*) font;
	int seq = ff->auto_number;

	char *key = make_key( seq );	

	srt_seq_t *s = NULL;
	if( vevo_property_get( ff->dictionary, key, 0, &s ) != VEVO_NO_ERROR )
	{
		free(key);
		return;
	}
	font_lock( ff );

	s->x = x;
	s->y = y;

	font_unlock( ff );
	free(key);
}

void    vj_font_set_size_and_font( void *font, int f_id, int size )
{
	vj_font_t *ff = (vj_font_t*) font;
	if( f_id < 0 || f_id >= ff->font_index  || size < 6 || size > 400 )
		return ;
	int seq = ff->auto_number;

	char *key = make_key( seq );	

	srt_seq_t *s = NULL;
	if( vevo_property_get( ff->dictionary, key, 0, &s ) != VEVO_NO_ERROR )
	{
		free(key);
		return;
	}

	font_lock( ff );
	
	s->font = f_id;
	s->size  = size;

	veejay_msg(VEEJAY_MSG_DEBUG, "Selected font '%s', size %d",
		ff->font_list[s->font], s->size );
	
	font_unlock( ff );

	free(key);	
}

void    vj_font_update_text( void *font, long s1, long s2, int seq, char *text)
{
	vj_font_t *ff = (vj_font_t*) font;
	srt_seq_t *s = NULL;
	void *srt = NULL;
	if(seq == 0 )
		seq = ff->auto_number;
	char *key = make_key( seq );
	int error = vevo_property_get( ff->dictionary, key, 0, &srt );
	if( error != VEVO_NO_ERROR )
	{
		veejay_msg(0, "No such subtitle '%s'",key);
		free(key);
		return;
	}
	else {
		veejay_msg(0, "Update subtitle %s to '%s' , play from %ld to %ld",
			key,text,s1,s2);
	}	

	if(s1 < 0 ) s1 = 0;
	if(s2 < 0 ) s2 = 0;

	if( (s2-s1) > ff->index_len || ff->index_len <= 0 ) {
		if(!vj_font_prepare( ff,s1,s2 ) ) {
			while(s2<=s1)
				s2++;
			if(!vj_font_prepare(ff,s1,s2)) {
				veejay_msg(0, "Error updating subtitle %s",key);	
				free(key);
				return;
			}
		}
	}


	s = (srt_seq_t*) srt;
	font_lock( ff );
	if( s->text )
		free(s->text);
	s->text = vj_strdup( text );
	s->start = s1;
	s->end = s2;

	ff->auto_number = seq;

	font_unlock( ff );
	free(key);	
}

char **vj_font_get_sequences( void *font )
{
	vj_font_t *f = (vj_font_t*) font;
	char **items = vevo_list_properties( f->dictionary );
	if(!items)
		return NULL;
	int i;
	int j=0;
	int len=0;
	
	for( i = 0; items[i] != NULL ; i ++ )
		len ++;
	
	if( len <= 0 )
		return NULL;
	
	char **res = (char**) vj_calloc(sizeof(char*) * (len+1));
	for( i = 0; items[i] != NULL ; i ++ )
	{
		srt_seq_t *s = NULL;
		if( vevo_property_get( f->dictionary, items[i], 0,&s ) == VEVO_NO_ERROR )
		{
			char tmp[16];
			snprintf(tmp,sizeof(tmp),"%d", s->id );
			res[j] = vj_strdup(tmp);
			j++;
		}	
		free(items[i]);
		
	}
	free(items);
	if( j == 0 )
	{
		free(res);
		return NULL;
	}

	return res;
}

#define MAX_FONTS 250

char 	**vj_font_get_all_fonts( void *font )
{
	vj_font_t *f = (vj_font_t*) font;
	int i;
	if( f->font_index <= 0 )
		return NULL;
	
	char **res = (char**) vj_calloc(sizeof(char*) * (f->font_index +1) );
	for( i =0; i < f->font_index ;i ++ )
		res[i] = vj_strdup( (char*) f->font_list[i] );
	return res;
}

static int	dir_selector( const struct dirent *dir )
{	
	return 1;
}

static	int	is_ttf( const char *file )
{
	if(strstr( file, ".ttf" ) || strstr( file, ".TTF" ) || strstr( file, "PFA" ) ||
			strstr(file, "pfa" ) || strstr( file, "pcf" ) || strstr( file, "PCF" ) )
		return 1;
	return 0;
}

static	int	try_deepen( vj_font_t *f , char *path )
{
	if(!path) return 0;

	struct stat l;
	memset( &l, 0, sizeof(struct stat) );
	if( lstat( path, &l ) < 0 )
		return 0;

	if( S_ISLNK( l.st_mode ) )
	{
		memset(&l,0,sizeof(struct stat));
		stat( path, &l );
	}

	if( S_ISDIR( l.st_mode ))
	{
		return 1;
	}

	if( S_ISREG( l.st_mode ))
	{
		if( is_ttf( path ) )
		{
			if( f->font_index < MAX_FONTS )
			{
				unsigned char *try_font = (unsigned char*) vj_strdup(path);
				if( get_font_name( f,try_font, f->font_index ) ) {
					f->font_table[f->font_index] = try_font;
					f->font_index ++;
				} else {
				   free(try_font);
				  }
				/*
				if( fnname ) {
					veejay_msg(VEEJAY_MSG_INFO, "FontName ='%s'",fnname );

				}else {
					veejay_msg(0, "Error '%s'",try_font);
				}
				
				
				if( test_font (f, try_font, f->font_index) )
				{
					f->font_table[ f->font_index ] = try_font;
					f->font_index ++;
				}
				else
				{
					free(try_font);
				}*/
			}
		}
	}
	return 0;
}

static int	find_fonts(vj_font_t *ec, char *path)
{
	struct dirent **files;
	int n = scandir(path, &files, dir_selector,alphasort);

	if(n < 0)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Error reading %s: %s", path, strerror(errno));
		return 0;
	}
	while( n -- )
	{
		char tmp[1024];
		snprintf( tmp, 1024, "%s/%s", path, files[n]->d_name );
		if( strcmp( files[n]->d_name , "." ) != 0 &&
				strcmp( files[n]->d_name, ".." ) != 0 )
		{
			if(try_deepen( ec, tmp ))
				find_fonts( ec, tmp );
		}
		free( files[n] );
	}
	free(files);
	return 1;
}


static unsigned char 	*select_font( vj_font_t *ec, int id )
{
	if( id < 0 || id >= ec->font_index )
		return NULL;

	return ec->font_table[id]; 	 
}


static	unsigned char	*get_font_name( vj_font_t *f,unsigned char *font, int id )
{
	unsigned char *string;
	int platform,encoding,lang;
	int error;

	static char *fontName = NULL;
	static int   fontLen  = 256;

	if( fontName == NULL ) {
		fontName = (char*) vj_malloc(sizeof(char) * fontLen );
	}
	if( fontName == NULL ) {
		return NULL;
	}
	
	FT_SfntName sn,qn,zn;	
	FT_UInt		snamei,snamec;

	FT_Face face;
	if ( (error = FT_New_Face( f->library, (char*)font, 0, &face )) != 0)
	{
		return 0;
	}

	memset( &qn, 0,sizeof( FT_SfntName ) );
	memset( &zn, 0, sizeof( FT_SfntName ));
	memset( &sn, 0, sizeof( FT_SfntName ));
	
	snamec = FT_Get_Sfnt_Name_Count(face);

	int found = 0;
	int tlen = 0;
	for(snamei=0;snamei<snamec;snamei++) {

		error = FT_Get_Sfnt_Name(face,snamei,&qn);
		if(error) 
			continue;

		platform = qn.platform_id;
		lang     = qn.language_id;
		encoding = qn.encoding_id;

		if( qn.string_len == 0 )
			continue;
		switch(qn.name_id) {
			case TT_NAME_ID_FULL_NAME:
				string = qn.string;
				tlen = qn.string_len;
				break;
			default:
				continue;
		}

		if(( platform == TT_PLATFORM_MICROSOFT) &&
		   ( encoding == TT_MS_ID_SYMBOL_CS ||
		     encoding == TT_MS_ID_UNICODE_CS ) &&
		   ( (lang&0xff) == 0x9 )) {
			found = 1;
			break;
		}
		if( platform == TT_PLATFORM_APPLE_UNICODE &&
		    lang == TT_MAC_LANGID_ENGLISH ) {
			found = 1;
			break;
		}
	}
	
	if(!found ) {
		return NULL;
	}

	while( (tlen/2+1) > fontLen ) {
		fontLen *= 2;
		fontName = (char*) realloc( fontName, fontLen );
		if( fontName == NULL ) {
			return NULL;
		}
	}

	int i;

	for( i=0; i < tlen; i +=2 ) {
	       fontName[i/2] = string[i+1];
	}
	fontName[tlen/2] = '\0';

	f->font_list[id] = (unsigned char*) vj_strdup( fontName );

	return (unsigned char*)fontName;
}

void vj_font_set_outline_and_border( void *font, int outline, int border)
{
	vj_font_t *ff = (vj_font_t*) font;
	
	int seq = ff->auto_number;
	char *key = make_key( seq );	

	srt_seq_t *s = NULL;
	if( vevo_property_get( ff->dictionary, key, 0, &s ) != VEVO_NO_ERROR )
	{
		free(key);
		return;
	}
	font_lock( ff );

	s->use_bg  = border;
	s->outline = outline;
	
	font_unlock( ff );

	free(key);
}

void vj_font_set_lncolor( void *font, int r, int g, int b, int a)
{
	vj_font_t *f = (vj_font_t*) font;
	
	int seq = f->auto_number;

	char *key = make_key( seq );	

	srt_seq_t *s = NULL;
	if( vevo_property_get( f->dictionary, key, 0, &s ) != VEVO_NO_ERROR )
	{
		free(key);
		return;
	}
	font_lock( f );

	s->ln[0] = r;
	s->ln[1] = g;
	s->ln[2] = b;
	s->alpha[2] = a;
	
	font_unlock( f );

	free(key);
}

void vj_font_set_bgcolor( void *font, int r, int g, int b,int a)
{
	vj_font_t *f = (vj_font_t*) font;
	
	int seq = f->auto_number;

	char *key = make_key( seq );	

	srt_seq_t *s = NULL;
	if( vevo_property_get( f->dictionary, key, 0, &s ) != VEVO_NO_ERROR )
	{
		free(key);
		return;
	}
	font_lock( f );

	s->bg[0] = r;
	s->bg[1] = g;
	s->bg[2] = b;
	s->alpha[1] = a;
	
	font_unlock( f );

	free(key);
}


void vj_font_set_fgcolor( void *font, int r, int g, int b, int a)
{
	vj_font_t *ff = (vj_font_t*) font;
	int seq = ff->auto_number;

	char *key = make_key( seq );	

	srt_seq_t *s = NULL;
	if( vevo_property_get( ff->dictionary, key, 0, &s ) != VEVO_NO_ERROR )
	{
		free(key);
		return;
	}
	font_lock( ff );

	s->fg[0] = r;
	s->fg[1] = g;
	s->fg[2] = b;
	s->alpha[0] = a;
	
	font_unlock( ff );

	free(key);
}

void	vj_font_dictionary_destroy( void *font, void *dict )
{
	char **items = vevo_list_properties(dict );
	if(!items) {
		vpf(dict);
		dict = NULL;
		return;
	}

	int i;
	for( i = 0; items[i] != NULL ; i ++ )
	{
		srt_seq_t *s = NULL;
		if( vevo_property_get( dict, items[i], 0,&s ) == VEVO_NO_ERROR )
		{
			free(s->text);
			free(s);
		}
		free(items[i]);
	}
	free(items);
	vpf( dict );
	dict = NULL;
}


void	vj_font_destroy(void *ctx)
{
	if(!ctx)
		return;

	vj_font_t *f = (vj_font_t*) ctx;

	if( f->face )
	{
		int c;
		for( c = 0; c < 256 ; c ++)
		{
			if( f->glyphs[c] )
				FT_Done_Glyph( f->glyphs[c] );
			f->glyphs[c] = NULL;
		}
		FT_Done_Face( f->face );
	}

	if(f->plain)
		vj_font_dictionary_destroy( f,f->plain );
	
	FT_Done_FreeType( f->library );
	
	int i;
	for( i = 0 ; i < MAX_FONTS ;i ++ )
	{
		if( f->font_list[i] )
			free(f->font_list[i]);
		if( f->font_table[i] )
			free(f->font_table[i]);
	}
//	free( f->text_buffer );	
	free( f->font_table );
	free( f->font_list );
	free( f );
}

static	void	fallback_font(vj_font_t *f)
{
	f->current_font = get_default_font( f );
		
	while( (f->font = select_font(f, f->current_font )) == NULL )
	{
		f->current_font ++;
		if( f->current_font >= f->font_index )
		{
			veejay_msg(0, "No more fonts to try");
			vj_font_destroy( f );
		}		
	}

}

static int	configure(vj_font_t *f, int size, int font)
{
	int c,error;
	FT_BBox bbox;
	int yMax,yMin,xMax,xMin;

	f->current_font = font;
	f->font = select_font( f , font );
	if(f->font == NULL )
	{
		fallback_font( f );
	}

//	veejay_msg(VEEJAY_MSG_DEBUG, "Using font %s, size %d (#%d)", f->font, size, font );
	veejay_memset( selected_default_font, 0, sizeof(selected_default_font));
	strncpy( selected_default_font, (char*)f->font,strlen((char*)f->font)) ;

	if( f->face )
	{
		for( c = 0; c < 256 ; c ++)
		{
			if( f->glyphs[c] )
				FT_Done_Glyph( f->glyphs[c] );
			f->glyphs[c] = NULL;
		}
		FT_Done_Face( f->face );
	}

	if ( (error = FT_New_Face( f->library, (char*)f->font, 0, &(f->face) )) != 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Cannot load face: %s (error #%d)\n ", f->font, error);
		return 0;
	}
	
	f->current_size = size;
	if ( (error = FT_Set_Pixel_Sizes( f->face, 0, f->current_size)) != 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot set font size to %d pixels (error #%d)\n",
				f->current_size, error );
		return 0;
	}

	f->use_kerning	=	FT_HAS_KERNING(f->face);
	
	yMax = -32000;
	yMin = 32000;
	xMax = -32000;
	xMin = 32000;

	for( c = 0; c < 256 ; c ++)
	{
		// load char
		error = FT_Load_Char( f->face, (unsigned char) c , FT_LOAD_RENDER | FT_LOAD_MONOCHROME );
		if(!error)
		{
			f->bitmaps[c]		= f->face->glyph->bitmap;
			f->bitmap_left[c]	= f->face->glyph->bitmap_left;
			f->bitmap_top[c]	= f->face->glyph->bitmap_top;
			f->advance[c]		= f->face->glyph->advance.x >> 6;

			
			FT_Get_Glyph( f->face->glyph, &(f->glyphs[c]) );
		}	

		f->glyphs_index[c] = FT_Get_Char_Index( f->face, (unsigned char) c );
		if( f->glyphs_index[c] )
		{
			FT_Glyph_Get_CBox( f->glyphs[ c ] , ft_glyph_bbox_pixels, &bbox);
			if( bbox.yMax > yMax )
				yMax = bbox.yMax;
			if( bbox.yMin < yMin )
				yMin = bbox.yMin;
			if( bbox.xMax > xMax )
				xMax = bbox.xMax;
			if( bbox.xMin < xMin )
				xMin = bbox.xMin;
		}
	}

	f->text_height = yMax - yMin;
	f->text_width  = xMax - xMin;
	f->baseline = yMax;

	return 1;
}

void	*vj_font_get_plain_dict( void *font )
{
	vj_font_t *f = (vj_font_t*) font;
	return f->plain;
}

void	vj_font_set_osd_text(void *font, char *text )
{
	vj_font_t *f = (vj_font_t*) font;
	if(f->add)	
		free(f->add);
	f->add = vj_strdup( text );
}

void	vj_font_set_dict( void *font, void *dict )
{
	vj_font_t *f = (vj_font_t*) font;
	if( f == NULL ) 
		return;
	f->dictionary = dict;
}

void	*vj_font_get_dict(void *font)
{
	vj_font_t *f = (vj_font_t*) font;
	if(f == NULL )
		return NULL;
	return f->dictionary;
}

static	int	compare_strings( const void *p1, const void *p2 )
{

	return strcoll( (const char *) p1, (const char *) p2 );
}

static	int	get_default_font( vj_font_t *f )
{
	static struct
	{
		char *name;
	} default_fonts[] = {
		{ "Arab (Regular)"},
		{ "Mashq (Regular)" },
		{ "DejaVu Sans (Bold)" },
		{ NULL },	
	};
	int i,j;
	for( i = 0; i < f->font_index; i ++ )
	{
		for( j = 0; default_fonts[j].name != NULL ; j ++ )
		{	
			if( f->font_list[i])
			{
				if( strcasecmp( default_fonts[j].name, (char*) f->font_list[i] ) == 0 )
				{
					veejay_msg(VEEJAY_MSG_DEBUG,"Using default font '%s'", default_fonts[j].name );
					return i;
				}
			}
		}
	}
	return 0;
}

void	*vj_font_init( int w, int h, float fps, int is_osd )
{
	int error=0;
	vj_font_t *f = (vj_font_t*) vj_calloc(sizeof(vj_font_t));
	f->text = NULL;
	f->x = 0;
	f->y = 0;
	f->w = w;
	f->h = h;
	f->auto_number = 1;
	f->index_len = 0;
	f->fgcolor[0] = 235;
	f->fgcolor[1] = 128;
	f->fgcolor[2] = 128;
	f->bgcolor[0] = 16;
	f->bgcolor[1] = 128;
	f->bgcolor[2] = 128;
	f->lncolor[0] = 200;
	f->lncolor[1] = 128;
	f->lncolor[2] = 128;
	f->alpha[0] = 0;
	f->alpha[1] = 0;
	f->alpha[2] = 0;
	f->bg = 0;
	f->outline = 0;
	f->text_height = 0;
	int tmp = ((w / 100) * 3) -1;
	if(tmp>15) tmp = 14;
	if(tmp<9) tmp = 9;
	f->current_size = (is_osd ? (tmp): 40);

	f->fps  = fps;
	f->index = NULL;
	f->font_table = (unsigned char**) vj_calloc(sizeof(unsigned char*) * MAX_FONTS );
	f->font_list = (unsigned char**) vj_calloc(sizeof(unsigned char*) * MAX_FONTS);
	f->font_index =0;
	f->plain = vpn( VEVO_ANONYMOUS_PORT );

	if ( (error = FT_Init_FreeType(&(f->library))) != 0 )
	{
		free(f->font_table);
		free(f->font_list);
		free(f);
		veejay_msg(VEEJAY_MSG_ERROR,"Cannot load FreeType (error #%d) \n",error);
		return NULL;
	}


	find_fonts(f,"/usr/X11R6/lib/X11/fonts/TTF");
	find_fonts(f,"/usr/X11R6/lib/X11/fonts/Type1");
	find_fonts(f,"/usr/X11R6/lib/X11/truetype");
	find_fonts(f,"/usr/X11R6/lib/X11/TrueType");
	find_fonts(f,"/usr/share/fonts/truetype");
	find_fonts(f, "/usr/share/fonts/TTF");
	
	if( f->font_index <= 0 )
	{
		char *home = getenv("HOME");
		char path[1024];
		snprintf(path,1024,"%s/.veejay/fonts",home);
		veejay_msg(VEEJAY_MSG_ERROR, "No TrueType fonts found");

		find_fonts(f,path);
		if( f->font_index <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Please put some TrueType font files in %s",path);
			return NULL;
		} 
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "Loaded %d TrueType fonts", f->font_index );

	qsort( f->font_table, f->font_index, sizeof(char*), compare_strings );
	qsort( f->font_list,  f->font_index, sizeof(char*), compare_strings );

	int df = get_default_font( f );
	
	while(!configure( f, f->current_size, df ))
	{
		f->current_font ++;
		if( f->current_font >= f->font_index )
		{
			veejay_msg(0, "It seems that all loaded fonts are not working. Bye");
			vj_font_destroy( f );
			return NULL;
		}		
	}

//	f->text_buffer = (srt_cycle_t*)
//		vj_calloc(sizeof(srt_cycle_t) *	(sizeof(srt_cycle_t)*22500));
//	f->text_max_size = 22500;

	pthread_mutex_init( &(f->mutex), NULL );
	
	return f;
}
void	*vj_font_single_init( int w, int h, float fps,char *path )
{
	int error=0;
	vj_font_t *f = (vj_font_t*) vj_calloc(sizeof(vj_font_t));
	f->text = NULL;
	f->x = 0;
	f->y = 0;
	f->w = w;
	f->h = h;
	f->auto_number = 1;
	f->index_len = 0;
	f->fgcolor[0] = 235;
	f->fgcolor[1] = 128;
	f->fgcolor[2] = 128;
	f->bgcolor[0] = 16;
	f->bgcolor[1] = 128;
	f->bgcolor[2] = 128;
	f->lncolor[0] = 200;
	f->lncolor[1] = 128;
	f->lncolor[2] = 128;
	f->alpha[0] = 0;
	f->alpha[1] = 0;
	f->alpha[2] = 0;
	f->bg = 0;
	f->outline = 0;
	f->text_height = 0;
	
	int tmp = (w / 100) * 3;
	if(tmp>18) tmp = 18;
	if(tmp<11) tmp = 11;
	f->current_size = tmp;

	f->fps  = fps;
	f->index = NULL;
	f->font_table = (unsigned char**) vj_calloc(sizeof(unsigned char*) * MAX_FONTS );
	f->font_list = (unsigned char**) vj_calloc(sizeof(unsigned char*) * MAX_FONTS);
	f->font_index =0;
	f->plain = vpn( VEVO_ANONYMOUS_PORT );

	if ( (error = FT_Init_FreeType(&(f->library))) != 0 )
	{
		free(f->font_table);
		free(f->font_list);
		free(f);
		veejay_msg(VEEJAY_MSG_ERROR,"Cannot load FreeType (error #%d) \n",error);
		return NULL;
	}

	char fontpath[1024];	
	snprintf(fontpath,1024,"%s/fonts",path);

	find_fonts(f,fontpath);
	if( f->font_index <= 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Please put a TrueType font file for the OSD in %s",fontpath);
		find_fonts( f, "/usr/share/fonts/truetype/freefont" );
		if( f->font_index <= 0 )
			find_fonts( f, "/usr/share/fonts/TTF");

		if( f->font_index <= 0 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Can't read default truetype font path /usr/share/fonts/truetype/freefont");
			return NULL;
		}
	}

	veejay_msg(VEEJAY_MSG_INFO, "Loaded %d TrueType fonts", f->font_index );
	qsort( f->font_table, f->font_index, sizeof(char*), compare_strings );
	qsort( f->font_list,  f->font_index, sizeof(char*), compare_strings );

	int df = get_default_font( f );
	
	while(!configure( f, f->current_size, df ))
	{
		f->current_font ++;
		if( f->current_font >= f->font_index )
		{
			veejay_msg(0, "It seems that all loaded fonts are not working. Bye");
			vj_font_destroy( f );
			return NULL;
		}		
	}

//	f->text_buffer = (srt_cycle_t*)
//		vj_calloc(sizeof(srt_cycle_t) *	(sizeof(srt_cycle_t)*22500));
//	f->text_max_size = 22500;

	pthread_mutex_init( &(f->mutex), NULL );
	
	return f;
}

static void draw_glyph( 
	vj_font_t *f,
	VJFrame *picture,
	FT_Bitmap *bitmap,
	unsigned int x,
	unsigned int y,
	unsigned int width,
	unsigned int height,	
	uint8_t *yuv_fgcolor,
	uint8_t *yuv_lncolor,
	int outline)
{
	int r, c;
	int spixel, dpixel[3], in_glyph=0;
	dpixel[2] = 128; dpixel[1] = 128;

	if( x < 0 ) x = 0; else if ( x > width ) x = width;
	if( y < 0 ) y = 0; else if ( y > height ) y = height;

	uint8_t *Y = picture->data[0];
	uint8_t *U = picture->data[1];
	uint8_t *V = picture->data[2];

	uint8_t	*bitbuffer = bitmap->buffer;
	uint32_t bitmap_rows = bitmap->rows;
	uint32_t bitmap_wid  = bitmap->width;
	uint32_t bitmap_pitch = bitmap->pitch;

	int p,left,top,bot,pos;

    if (bitmap->pixel_mode == ft_pixel_mode_mono)
    {
        in_glyph = 0;
	for (r=0; (r < bitmap_rows) && (r+y < height); r++)
	{
		for (c=0; (c < bitmap_wid) && (c+x < width); c++)
		{
			p  = (c+x) + ((y+r)*width);
			left  = (c+x-1) + ((y+r)*width);    
			top   = (c+x) + ((y+r-1)*width);
	    		bot   = (c+x) + ((y+r+1)*width);
			
			dpixel[0] = Y[ p ];
			dpixel[1] = U[ p ];
			dpixel[2] = V[ p ];

			pos = r * bitmap_pitch + (c >> 3 );

	   //   		spixel = bitmap->buffer[r*bitmap->pitch +c/8] & (0x80>>(c%8)); 
	     
			spixel = bitbuffer[ pos ] & ( 0x80 >> ( c % 8 ));
 
	     	 	if (spixel) 
	     	 	{
	     			dpixel[0] = yuv_fgcolor[0];
				dpixel[1] = yuv_fgcolor[1];
				dpixel[2] = yuv_fgcolor[2];
	      		} 
	      		if (outline)
			{
				if ( (!in_glyph) && (spixel) )
				{
		      			in_glyph = 1;
		      			if (c-1 >= 0)
		      			{
						Y[ left ] = yuv_lncolor[0];	
						U[ left ] = yuv_lncolor[1];
						V[ left ] = yuv_lncolor[2]; 
		      			}
		    		}
		  		else if ( (in_glyph) && (!spixel) )
		    		{
		      			in_glyph = 0;
		      			dpixel[0] = yuv_lncolor[0];
		      			dpixel[1] = yuv_lncolor[1];
		      			dpixel[2] = yuv_lncolor[2];
			    	}
		 
		  		if (in_glyph) 
		    		{
		      			if ( (r-1 >= 0) && (! bitbuffer[(r-1)*bitmap->pitch +c/8] & (0x80>>(c%8))) )
					{
						Y[ top ] = yuv_lncolor[0];	
						U[ top ] = yuv_lncolor[1];
						V[ top ] = yuv_lncolor[2]; 
					}
			
					if ( (r+1 < height) && (! bitbuffer[(r+1)*bitmap->pitch +c/8] & (0x80>>(c%8))) )
					{
						Y[ bot  ] = yuv_lncolor[0];	
						U[ bot ] = yuv_lncolor[1];
						V[ bot ] = yuv_lncolor[2]; 
		      			}
		    		}
			}
			Y[ p ] = dpixel[0];	
			U[ p ] = dpixel[1];
			V[ p ] = dpixel[2]; 
	    	}
	}
    }
}

// in struct: line color [3] , alpha fg, bg and line color
// extras in sequence: alpha (3x), linecolor( 3x )
static void draw_transparent_glyph( 
	vj_font_t *f,
	VJFrame *picture,
	FT_Bitmap *bitmap,
	unsigned int x,
	unsigned int y,
	unsigned int width,
	unsigned int height,	
	uint8_t *yuv_fgcolor,
	uint8_t *yuv_lncolor,
	int outline,
	int fgop,
	int bgop)
{
	int r, c;
	int spixel, dpixel[3], in_glyph=0;
	dpixel[2] = 128; dpixel[1] = 128;


	uint8_t *Y = picture->data[0];
	uint8_t *U = picture->data[1];
	uint8_t *V = picture->data[2];

	const uint8_t fop1 = fgop;
	const uint8_t fop0 = 255 - fop1;
	const uint8_t bop1 = bgop;
	const uint8_t bop0 = 255 - bop1;

	// opacity for line color , fgcolor and bgcolor. is bg color needed here? 	
    	if (bitmap->pixel_mode == ft_pixel_mode_mono)
    	{

		in_glyph = 0;
		for (r=0; (r < bitmap->rows) && (r+y < height); r++)
		{
			for (c=0; (c < bitmap->width) && (c+x < width); c++)
			{
				int p  = (c+x) + ((y+r)*width);
				int left  = (c+x-1) + ((y+r)*width);    
				int top   = (c+x) + ((y+r-1)*width);
	    			int bot   = (c+x) + ((y+r+1)*width);
					
				dpixel[0] = Y[ p ];
				dpixel[1] = U[ p ];
				dpixel[2] = V[ p ];

	   	   		spixel = bitmap->buffer[r*bitmap->pitch +c/8] & (0x80>>(c%8)); 
	      
	     		 	if (spixel) 
	     	 		{
	     				dpixel[0] = (Y[p] * fop0 + yuv_fgcolor[0] * fop1 ) >> 8;
					dpixel[1] = (U[p] * fop0 + yuv_fgcolor[1] * fop1 ) >> 8;
					dpixel[2] = (V[p] * fop0 + yuv_fgcolor[2] * fop1 ) >> 8;
	      			} 
	      			if (outline)
				{
					if ( (!in_glyph) && (spixel) )
					{
		      				in_glyph = 1;
		 	     			if (c-1 >= 0)
		      				{
							Y[ left ] = (Y[p] * bop0 + yuv_lncolor[0] * bop1)>>8;	
							U[ left ] = (U[p] * bop0 + yuv_lncolor[1] * bop1)>>8;
							V[ left ] = (V[p] * bop0 + yuv_lncolor[2] * bop1)>>8; 
		      				}
		    			}
		  			else if ( (in_glyph) && (!spixel) )
		    			{
		      				in_glyph = 0;
		      				dpixel[0] = (Y[p] * bop0 + yuv_lncolor[0] * bop1) >> 8;
		      				dpixel[1] = (U[p] * bop0 + yuv_lncolor[1] * bop1) >> 8;
		      				dpixel[2] = (V[p] * bop0 + yuv_lncolor[2] * bop1) >> 8;
			    		}
		 
		  			if (in_glyph) 
		    			{
		      				if ( (r-1 >= 0) && (! bitmap->buffer[(r-1)*bitmap->pitch +c/8] & (0x80>>(c%8))) )
						{
							Y[ top ] = (Y[p] * bop0 + yuv_lncolor[0] * bop1)>>8;	
							U[ top ] = (U[p] * bop0 + yuv_lncolor[1] * bop1)>>8;
							V[ top ] = (V[p] * bop0 + yuv_lncolor[2] * bop1)>>8; 
						}
			
						if ( (r+1 < height) && (! bitmap->buffer[(r+1)*bitmap->pitch +c/8] & (0x80>>(c%8))) )
						{
							Y[ bot ] = (Y[p] * bop0 + yuv_lncolor[0] * bop1)>>8;	
							U[ bot ] = (U[p] * bop0 + yuv_lncolor[1] * bop1)>>8;
							V[ bot ] = (V[p] * bop0 + yuv_lncolor[2] * bop1)>>8; 
		      				}
		    			}
				}
				Y[ p ] = dpixel[0];	
				U[ p ] = dpixel[1];
				V[ p ] = dpixel[2]; 
	    		}
		}
    	}
}

static inline void draw_transparent_box(
		VJFrame *picture,
		unsigned int x,
		unsigned int y,
		unsigned int width,
		unsigned int height,
	       	uint8_t *yuv_color,
		uint8_t opacity)
{
  const int op1 = opacity;
  const int op0 = 255 - op1;
  const int w = picture->width;
  int i, j;
 
  uint8_t *A[3] = {
	  picture->data[0],
	  picture->data[1],
	  picture->data[2]
  };

  int p;
  
  for (j = y; j < height; j++)
    for (i = x; i < width; i++) 
      {	
	p = (i + (j * w));
	A[0][p] = (op0 * A[0][ p ] + op1 * yuv_color[0]) >> 8;
	A[1][p] = (op0 * A[1][ p ] + op1 * yuv_color[1]) >> 8;
	A[2][p] = (op0 * A[2][ p ] + op1 * yuv_color[2]) >> 8;	
      }
  
}

static inline void draw_box(VJFrame *picture, unsigned int x, unsigned int y, unsigned int width, unsigned int height, uint8_t *yuv_color)
{
  int i, j;

  for (j = y; j < height; j++)
    for (i = x; i < width; i++) 
      {	
	picture->data[0][ i + (j * picture->width ) ] = yuv_color[0];	
	picture->data[1][ i + (j * picture->width ) ] = yuv_color[1];
	picture->data[2][ i + (j * picture->width ) ] = yuv_color[2]; 
      }
  
}

#define MAXSIZE_TEXT 1024

static void vj_font_text_render(vj_font_t *f, srt_seq_t *seq, void *_picture )
{
	int size = strlen(seq->text);
	FT_Face face = f->face;
	FT_GlyphSlot  slot = face->glyph;
	FT_Vector pos[MAXSIZE_TEXT];  
	FT_Vector delta;
  
	unsigned char c;
	int x = 0, y = 0, i=0;
	int str_w, str_w_max;

	VJFrame *picture = (VJFrame*) _picture;
	int width = picture->width;
	int height = picture->height;

	int x1 = f->x;
	int y1 = f->y;
	
	str_w = str_w_max = 0;

	x = f->x; 
	y = f->y;

	char *text = seq->text;

	for (i=0; i < size; i++)
	{
		c = text[i];
		if ( (f->use_kerning) && (i > 0) && (f->glyphs_index[c]) )
		{
			FT_Get_Kerning(
				f->face, 
				f->glyphs_index[c],
				//	f->glyphs_index[ text[i-1] ], 
				f->glyphs_index[c],
				ft_kerning_default, 
				&delta
				);
	 
			x += delta.x >> 6;
		}
    
	        if( isblank( c ) || c == 20 )
		{
			f->advance[c] = f->current_size;
			if( (x + f->current_size) >= width )
			{
				str_w = width = f->x - 1;
				y += f->text_height;
				x = f->x;
			}
		}
		else
		if (( (x + f->advance[ c ]) >= width ) || ( c == '\n' ))
		{
			str_w = width - f->x - 1;
			y += f->text_height;
			x = f->x;
		}

		pos[i].x = x + f->bitmap_left[c];
      		pos[i].y = y - f->bitmap_top[c] + f->baseline;
      		x += f->advance[c];

		if (str_w > str_w_max)
			str_w_max = str_w;
	}
	
  	if (f->bg)
	{
		if ( str_w_max + f->x >= width )
			str_w_max = width - f->x - 1;
		if ( y >= height )
			y = height - 1 - 2*f->y;

		if( str_w_max == 0 )
			str_w_max = x1 + (x - x1);
	
		int bw = str_w_max;
		int bh = y - y1;
	        if(bh <= 0 )
			bh = y1	+ f->current_size;
		
		if( f->alpha[1] == 0 )
		{
			draw_box(
				picture,
				x1,
				y1,
				bw,
				bh,
				f->bgcolor
			);
		}
		else
		{
			draw_transparent_box(
				picture,
				x1,
				y1,
				bw,
				bh,
				f->bgcolor,
				f->alpha[1] );
		}	
 	}
	
 	for (i=0; i < size; i++)
    	{
		c = text[i];
		if (  ((c == '_') && ((char*)text == (char*)f->text) ) || /* skip '_' (consider as space) 
					     IF text was specified in cmd line 
					     (which doesn't like neasted quotes)  */
	 		 ( c == '\n' )) /* Skip new line char, just go to new line */
			continue;

		if( f->alpha[0] || f->alpha[2] )
		{
			draw_transparent_glyph( f, picture, &(f->bitmaps[c]),
					pos[i].x,pos[i].y,width,height,
					f->fgcolor,f->lncolor, f->outline,
					f->alpha[0],f->alpha[2] );
		}
		else
		{
			draw_glyph( f,picture, &(f->bitmaps[ c ]),
					pos[i].x,pos[i].y,width, height,
					f->fgcolor,f->lncolor,f->outline );
	    	}
      		x += slot->advance.x >> 6;
    	}

}

static	 int	num_nl( char *str , int len ) {
	int i;
	int sum = 0;
	for( i = 0; i < len ; i ++ )
		if( str[i] == '\n' )
			sum ++;
	return sum;
}

static void vj_font_text_osd_render(vj_font_t *f, void *_picture, int x, int y )
{
	FT_Face face = f->face;
	FT_GlyphSlot  slot = face->glyph;
	FT_Vector pos[MAXSIZE_TEXT];  
	FT_Vector delta;
	unsigned char c;
	int i=0;
	int str_w, str_w_max;
	VJFrame *picture = (VJFrame*) _picture;
	int width = picture->width;
	int height = picture->height;
	int x1,y1;
	str_w = str_w_max = 0;

	if(!f->add)
		return;

	int size = strlen( f->add );
	if( size <= 0 ) 
		return;
	
	if( y == -1 )
	{
		int n = num_nl(f->add,size);
		if( n > 4 )
			y = 0;
		else
			y = picture->height - (f->current_size * n ) - f->current_size - 8;
		if( width < 512 )
			y -=  (f->current_size * n ) - f->current_size - 8;
		if ( y < 0 ) 
			y = 0;
	}


	x1 = x;
	y1 = y;	

	unsigned int str_wi = 0;
	unsigned char *text = (unsigned char*) f->add;

	for (i=0; i < size; i++)
	{
		c = text[i];
		if ( (f->use_kerning) && (i > 0) && (f->glyphs_index[c]) )
		{
			FT_Get_Kerning(
				f->face, 
				f->glyphs_index[c],
				f->glyphs_index[c],
				ft_kerning_default, 
				&delta
				);
	 
			x += delta.x >> 6;
		}
    
	        if( isblank( c ) || c == 20 )
		{
			f->advance[c] = f->current_size;
			if( (x + f->current_size) >= width )
			{
				str_w = width = f->x - 1;
				y += f->text_height;
				x = f->x;
			}
		}
		else
		if (( (x + f->advance[ c ]) >= width ) || ( c == '\n' ))
		{
			str_w = width - f->x - 1;
			y += f->text_height;
			if( str_wi < x )
				str_wi = x;
			x = f->x;
		}

		pos[i].x = x + f->bitmap_left[c];
      		pos[i].y = y - f->bitmap_top[c] + f->baseline;
      		x += f->advance[c];

		if (str_w > str_w_max)
			str_w_max = str_w;
	}
	
//	if ( str_w_max + f->x >= width )
//		str_w_max = width - f->x - 1;

	if ( y >= height )
		y = height - 1 - 2*f->y;

//	if( str_w_max == 0 )
	str_w_max = (x - x1);
	
	int bh = y - y1;
	if(bh <= 0 )
		bh = y1	+ f->current_size + 4;

	draw_transparent_box(
			picture,
			x1,
			y1,
			str_wi,//picture->width,
			bh,
			f->bgcolor,
			80 );
	
 	for (i=0; i < size; i++)
    	{
		c = text[i];
		if (  ((c == (unsigned char) '_') && (text == f->text) ) || /* skip '_' (consider as space) 
					     IF text was specified in cmd line 
					     (which doesn't like neasted quotes)  */
	 		 ( c == '\n' )) /* Skip new line char, just go to new line */
			continue;
		draw_glyph( f,picture, &(f->bitmaps[ c ]),
				pos[i].x,pos[i].y,width, height,
				f->fgcolor,f->lncolor,f->outline );
	    	
      		x += slot->advance.x >> 6;
    	}
}

char	*vj_font_default()
{
	int n = strlen( selected_default_font );
	if( n <= 0 )
		return NULL;

	return selected_default_font;
}


int	vj_font_norender(void *ctx, long position)
{
	if(!ctx)
		return 0;
	vj_font_t *f = (vj_font_t *) ctx;

	if(!f->dictionary  )
		return 0;

	int i;
	int jobs = 0;
	for( i = 0; i < f->index_len; i ++ ) {
		srt_seq_t *s = f->index[i];
		if(!s || !s->text) 
			continue;
		if(s->start <= position && s->end >= position ) {
			jobs ++;	
		}
	}

	return jobs;
}

void	vj_font_render_osd_status( void *ctx, void *_picture, char *status_str, int placement )
{
	vj_font_set_osd_text(ctx,status_str);
	if(placement == 1) {
		vj_font_text_osd_render( ctx, _picture, 5, 5 );
	}
	else
	{
		vj_font_text_osd_render( ctx, _picture, 5, -1 );
	}

}

void vj_font_render(void *ctx, void *_picture, long position)
{
	vj_font_t *f = (vj_font_t *) ctx;
	font_lock( f );
	int i;
	for( i = 0; i < f->index_len; i ++ ) {
		srt_seq_t *s = f->index[i];	
		if(!s)
			continue;
	
		if( position < s->start || position > s->end )
			continue;

		int   old_font = f->current_font;
		int   old_size = f->current_size;

		if( old_font != s->font || old_size != s->size )
			if(!configure( f, s->size, s->font ))
				if(!configure( f, old_size, old_font ))
					break;
		
		f->x = s->x;
		f->y = s->y;
		f->bg = s->use_bg;
		f->outline = s->outline;
		
		if(f->outline)
			_rgb2yuv( s->ln[0],s->ln[1],s->ln[2], f->lncolor[0],f->lncolor[1],f->lncolor[2]);	

		_rgb2yuv( s->fg[0],s->fg[1],s->fg[2], f->fgcolor[0],f->fgcolor[1],f->fgcolor[2] );
		if(f->bg)
			_rgb2yuv( s->bg[0],s->bg[1],s->bg[2], f->bgcolor[0],f->bgcolor[1],f->bgcolor[2] );

		f->alpha[0] = s->alpha[0];
		f->alpha[1] = s->alpha[1];
		f->alpha[2] = s->alpha[2];
			
		vj_font_text_render( f,s, _picture );

	}	

	font_unlock(f);
}



#endif

