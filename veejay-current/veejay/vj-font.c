/*
 * drawtext.c: print text over the screen
 * heavily modified to serve as font renderer in veejay
 ******************************************************************************
 * Author: Gustavo Sverzut Barbieri <gsbarbieri@yahoo.com.br>
 *         Niels Elburg <nelburg@looze.net> (Nov. 2006)
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
#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <libvje/vje.h>
#include <libvje/effects/common.h>
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

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#ifdef HAVE_FREETYPE
#include <fcntl.h>
#include <ft2build.h>
#include <freetype/ftsnames.h>
#include <freetype/ttnameid.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

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
	FT_Glyph   glyphs[ 255 ]; 
	FT_Bitmap  bitmaps[ 255 ];
	int        advance[ 255 ];
	int        bitmap_left[ 255 ];
	int        bitmap_top[ 255 ];
	unsigned int glyphs_index[ 255 ];
	int        text_height;
	int	   text_width;
	int	   current_font;
	int        baseline;
	int use_kerning;
	int current_size;
	char	**font_table;
	char    **font_list;
	int	auto_number;
	int	font_index;
	long		index_len;
	srt_cycle_t	**index;
	float		fps;
	void	*dictionary;
	void	*plain;
	int	time;
	char	*add;
	pthread_mutex_t	mutex;
} vj_font_t;


static int	configure(vj_font_t *f, int size, int font);
static char 	*make_key(int id);
static char 	*vj_font_pos_to_timecode( vj_font_t *font, long pos );
static long	vj_font_timecode_to_pos( vj_font_t *font, const char *tc );
static srt_seq_t	*vj_font_new_srt_sequence(vj_font_t *font, int id,char *text, long lo, long hi );
static void	vj_font_del_srt_sequence( vj_font_t *f, int seq_id );
static void	vj_font_store_srt_sequence( vj_font_t *f, srt_seq_t *s );
static int	font_selector( const struct dirent *dir );
static int      find_fonts(vj_font_t *ec, char *path);
static void     print_fonts(vj_font_t *ec);
static char 	*select_font( vj_font_t *ec, int id );
static  void    vj_font_substract_timecodes( vj_font_t *font, const char *tc_srt, long *lo, long *hi );
static char     *vj_font_split_strd( const char *str );
static char     *vj_font_split_str( const char *str );

static	int	get_default_font( vj_font_t *f );

static int	test_font( vj_font_t *f , const char *font, int id);

static	void	font_lock(vj_font_t *f)
{
	pthread_mutex_lock( &(f->mutex) );
}
static  void	font_unlock( vj_font_t *f )
{
	pthread_mutex_unlock( &(f->mutex) );
}
/*
static	srt_nodes_t	*index_node_new( srt_seq_t *s )
{
	srt_nodes_t *seq = (srt_nodes_t*) vj_calloc(sizeof(srt_nodes_t));
	seq->id = s->id;
	seq->next = NULL;
	return seq;
}*/

static	int	index_node_append( vj_font_t *f, srt_seq_t *s )
{
	int k; long i;
	for( i = s->start ; i <= s->end ; i ++ )
	{
		srt_cycle_t *q = f->index[ i ];
#ifdef STRICT_CHECKING
		assert( q != NULL );
#endif
		for( k = 0; k < 16; k ++ )
		{
			if( q->id[k] == 0 )
			{
				q->id[k] = s->id;
				break;
			}
		}
	}
	return 0;
}

static	void	index_node_remove( vj_font_t *f, srt_seq_t *s )
{
	int k; long i;
	for( i = s->start; i <= s->end; i ++ )
	{
		srt_cycle_t *q = f->index[i];
		if( q == NULL) continue;
		for( k = 0;k < 16; k ++ )
		{
			if( q->id[k] == s->id )
				q->id[k] =0;
		}
	}
}

static char *make_key(int id)
{
	char key[10];
	sprintf(key,"s%d",id);
	return strdup(key);
}

int	vj_font_srt_sequence_exists( void *font, int id )
{
	if(!font)
		return 0;
	vj_font_t *f = (vj_font_t*) font;
	if(!f->dictionary )
		return 0;

#ifdef STRICT_CHECKING
	assert( f->dictionary != NULL );
#endif
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
        memset(&tc, 0,sizeof(MPEG_timecode_t));
       	char tmp[20];
	
        y4m_ratio_t ratio = mpeg_conform_framerate( ff->fps );
        int n = mpeg_framerate_code( ratio );

        mpeg_timecode(&tc, pos, n, ff->fps );

        snprintf(tmp, 20, "%2d:%2.2d:%2.2d:%2.2d",
                tc.h, tc.m, tc.s, tc.f );

	return strdup(tmp);
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

static	void	vj_font_substract_timecodes( vj_font_t *font, const char *tc_srt, long *lo, long *hi )
{
	char tc1[20];
	char tc2[20];

	bzero(tc1,20);
	bzero(tc2,20);

	sscanf( tc_srt, "%s %*s %s", tc1,tc2 );

	*lo = vj_font_timecode_to_pos( font, tc1 );
	*hi = vj_font_timecode_to_pos( font, tc2 );
}

static char	*vj_font_split_strd( const char *str )
{
	const char *p = str;
	char  *res    = NULL;
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
	
	res = strndup( str, i );
	return res;
}

static char	*vj_font_split_str( const char *str )
{
	const char *p = str;
	char  *res    = NULL;
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
	
	res = strndup( str, i );
	return res;
}

static srt_seq_t 	*vj_font_new_srt_sequence( vj_font_t *f,int id,char *text, long lo, long hi )
{
	char tmp_key[16];
	srt_seq_t *s = (srt_seq_t*) vj_calloc(sizeof( srt_seq_t ));
	s->id   = id;
	s->text = strdup( text );
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
	s->key  = strdup(tmp_key);
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
#ifdef STRICT_CHECKING
	assert( f->dictionary != NULL );
#endif
	if(seq_id == 0 )
		seq_id = f->auto_number;
	
	char *key = make_key(seq_id );
	void *srt = NULL;

	
	int error = vevo_property_get( f->dictionary, key, 0, &srt ) ;
	if( error == VEVO_NO_ERROR )
	{
		srt_seq_t *s = (srt_seq_t*) srt;

		index_node_remove( f, s );

		free(s->text);
		free(s->key);
		free(s);
		vevo_property_set( f->dictionary, key, VEVO_ATOM_TYPE_VOIDPTR, 0,NULL );

	}
	free(key);
}

static void		vj_font_store_srt_sequence( vj_font_t *f, srt_seq_t *s )
{
#ifdef STRICT_CHECKING
	assert( f->dictionary != NULL );
#endif	
	void *srt = NULL;
	int error = vevo_property_get( f->dictionary, s->key, 0, &srt );
	if( error == VEVO_NO_ERROR )
	{
		srt_seq_t *old = (srt_seq_t*) srt;

		index_node_remove( f, old );

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
	else
	{
		index_node_append( f, s );
	}
}

int	vj_font_load_srt( void *font, const char *filename )
{
	vj_font_t *ff = (vj_font_t*) font;
	FILE *f = fopen( filename, "r" );
	unsigned int len = 0;
	unsigned int i;
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
	uint8_t *str = ff->fd_buf;
	int offset   = 0;


	font_lock( ff );
	while( offset < len )
	{	
		char *line = vj_font_split_str( str );
		if(!line)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to parse sequence ID in srt file");
			font_unlock( ff );
			return 0;
		}
		int   n  = strlen( line );

		offset += n;
		str += n;
		
		char *timecode = vj_font_split_str( str );
		if(!timecode)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to parse timecode in srt file");
			font_unlock(ff);
			return 0;
		}
		n = strlen( timecode );

		offset += n;
		str +=  n;

		char *text = vj_font_split_strd ( str );
		if(!text)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to parse subtitle text in srt file");
			font_unlock(ff);
			return 0;
		}
		n = strlen ( text );

		offset += n;
		str += n;

		long lo=0,hi=0;
		int seq_id = atoi( line );

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
        char **items = vevo_list_properties ( ff->dictionary );
        if(!items)
                return;
#ifdef STRICT_CHECKING
        assert( ff->dictionary != NULL );
#endif

	char buf[100];
	int i;
        for( i = 0; items[i] != NULL ; i ++ )
        {
                void *srt = NULL;
                if ( vevo_property_get( ff->dictionary, items[i], 0, &srt ) == VEVO_NO_ERROR )
                {
                        srt_seq_t *s = (srt_seq_t*) srt;
		
			xmlNodePtr *childnode = xmlNewChild( node, NULL, (const xmlChar*) "SUBTITLES" , NULL );
	
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
#ifdef STRICT_CHECKING
	assert( ff->dictionary != NULL );
#endif
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

#ifdef STRICT_CHECKING
	assert( ff->dictionary != NULL );
#endif
	if( vevo_property_get( ff->dictionary, key, 0, &srt ) != VEVO_NO_ERROR )
		return NULL;
	
	s = (srt_seq_t*) srt;
	
	int tcl1, tcl2;
	char *tc1 = vj_font_pos_to_timecode( ff, s->start );
	char *tc2 = vj_font_pos_to_timecode( ff, s->end );
	int len = strlen(s->text);
	tcl1 = strlen(tc1);
	tcl2 = strlen(tc2);

	uint8_t bg[3];
	uint8_t fg[3];

	
	sprintf( tmp, "%05d%09d%09d%02d%s%02d%s%03d%s%04d%04d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d",
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
	return strdup(tmp);
}

int	vj_font_new_text( void *font, char *text, long lo,long hi, int seq)
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

#ifdef STRICT_CHECKING
	assert( ff->dictionary != NULL );
#endif
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

#ifdef STRICT_CHECKING
	assert( ff->dictionary != NULL );
#endif
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
#ifdef STRICT_CHECKING
	assert( ff->dictionary != NULL );
#endif
	int error = vevo_property_get( ff->dictionary, key, 0, &srt );
	if( error != VEVO_NO_ERROR )
	{
		veejay_msg(0, "Subtitle sequence %d does not exist, code %d, used key '%s'",seq,error,key);
		free(key);
		return;
	}
	s = (srt_seq_t*) srt;

	font_lock( ff );
	
	index_node_remove( font, s );
	
	free(s->text);
	s->text = strdup( text );
	s->start = s1;
	s->end = s2;

	ff->auto_number = seq;

	index_node_append( font, s );

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
			sprintf(tmp, "%d", s->id );
			res[j] = strdup(tmp);
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
		res[i] = strdup( f->font_list[i] );
	return res;
}


static int	font_selector( const struct dirent *dir )
{	
	if(strstr(dir->d_name, ".ttf" )) return 1;
	if(strstr(dir->d_name, ".TTF" )) return 1;
	if(strstr(dir->d_name, ".pfa" )) return 1;
	if(strstr(dir->d_name, ".pcf.gz" )) return 1;
	return 0;
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
	int n = 0;
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
				char *try_font = strdup(path);
				if( test_font (f, try_font, f->font_index) )
				{
					f->font_table[ f->font_index ] = try_font;
					f->font_index ++;
				}
				else
				{
					free(try_font);
				}
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
		return 0;

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

static char 	*select_font( vj_font_t *ec, int id )
{
	if( id < 0 || id >= ec->font_index )
		return NULL;

	return ec->font_table[id]; 	 
}

static int	test_font( vj_font_t *f , const char *font, int id)
{
	char name[1024];
	FT_Face face;
	int error;

	FT_SfntName sn,qn,zn;	
	if ( (error = FT_New_Face( f->library, font, 0, &face )) != 0)
	{
		return 0;
	}

	memset( &qn, 0,sizeof( FT_SfntName ) );
	memset( &zn, 0, sizeof( FT_SfntName ));
	memset( &sn, 0, sizeof( FT_SfntName ));

	FT_Get_Sfnt_Name( face, TT_NAME_ID_FONT_FAMILY, &qn );
	FT_Get_Sfnt_Name( face, TT_NAME_ID_FONT_SUBFAMILY, &zn );


	if( !zn.string || !qn.string ||  qn.string_len <= 0 || zn.string_len <= 0 )
	{
		FT_Done_Face(face);
		return 0;
	}
	char *name1 = strndup( qn.string, qn.string_len );
	char *name2 = strndup( zn.string, zn.string_len );

	int n1 = strlen(name1);
	int n2 = n1 + strlen(name2);

	if( n2 <= 2 || (n2+n1) > 150)
	{
		FT_Done_Face(face);
		free(name1);
		free(name2);
		return 0;
	}
	
	snprintf( name,1024,"%s (%s)", name1, name2);
	
	f->font_list[id] = strdup( name );
	
	free(name1);
	free(name2);
	
	FT_Done_Face( face );
	return 1;
}



static void	print_fonts(vj_font_t *ec)
{
	int i;
	for(i =0 ; i < ec->font_index ; i ++ )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "[%03d] : [%s]", 
			i, ec->font_list[i] );
	}
}


void vj_font_set_outline_and_border( void *font, int outline, int border)
{
	vj_font_t *ff = (vj_font_t*) font;
	
	int seq = ff->auto_number;
	char *key = make_key( seq );	

#ifdef STRICT_CHECKING
	assert( ff->dictionary != NULL );
#endif
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

#ifdef STRICT_CHECKING
	assert( f->dictionary != NULL );
#endif
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

#ifdef STRICT_CHECKING
	assert( f->dictionary != NULL );
#endif
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

#ifdef STRICT_CHECKING
	assert( ff->dictionary != NULL );
#endif
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
	if(!items)
		return;

	vj_font_t *f = (vj_font_t*) font;

	int i;
	for( i = 0; items[i] != NULL ; i ++ )
	{
		srt_seq_t *s = NULL;
		if( vevo_property_get( dict, items[i], 0,&s ) == VEVO_NO_ERROR )
		{
			index_node_remove( f, s );

			free(s->text);
			free(s);
		}
		free(items[i]);
	}
	free(items);
	vevo_port_free( dict );
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
	if( f->index )
	{
		long k;
		for( k =0; k <= f->index_len ; k ++ )
		{
		  if( f->index[k] )
			free(f->index[k]);
		}
		free(f->index );
	}
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

	veejay_msg(VEEJAY_MSG_DEBUG, "Using font %s, size %d (#%d)", f->font, size, font );

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

	if ( (error = FT_New_Face( f->library, f->font, 0, &(f->face) )) != 0)
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

void	vj_font_print_credits(void *font, char *text)
{
	vj_font_t *f = (vj_font_t*) font;
	
	static const char *intro = 
		"A visual instrument for GNU/Linux\n";
	static const char *license =
		"This program is licensed as\n     Free Software (GNU/GPL version 2)\n\nFor more information see:\nhttp://veejay.dyne.org\nhttp://www.sourceforge.net/projects/veejay\nhttp://www.gnu.org";
	static const char *copyr =
		"(C) 2002-2006 Copyright N.Elburg et all\n";
	//@ create text to print
	sprintf(text, "This is Veejay version %s\n%s\n%s\n%s",VERSION,intro,copyr,license);
	
	
}


void	vj_font_customize_osd( void *font,void *uc, int type )
{
	vj_font_t *f = (vj_font_t*) font;
	veejay_t *v = (veejay_t*) uc;
	video_playback_setup *settings =v->settings;
	
	char buf[256];

	switch( v->uc->playback_mode )
	{
		case VJ_PLAYBACK_MODE_SAMPLE:
			sprintf(buf, "Sample %d|%d cache=%dMb cost=%d ms",
					v->uc->sample_id,
					sample_size()-1,
					sample_cache_used(0),
					v->real_fps );
			break;
		case VJ_PLAYBACK_MODE_TAG:
			sprintf(buf, "Stream %d|%d cost=%d ms",
					v->uc->sample_id,
					vj_tag_size(),
					v->real_fps);
			break;
		default:
			f->time = type;
			if( f->add )
				free(f->add );
			f->add = NULL;			
			return;
			break;
	}
	
	
	if(f->add)
		free(f->add);
	f->add = strdup( buf );
	f->time = type;
}

void	vj_font_set_constraints_and_dict( void *font, long lo, long hi, float fps, void *dict )
{
	vj_font_t *f = (vj_font_t*) font;

	long len = hi - lo + 1;

	veejay_msg(VEEJAY_MSG_DEBUG, "Subtitle: Dictionary %p, Lo = %ld , Hi = %ld, Fps = %f, font = %p",
			dict, lo,hi, fps , font);


	f->fps = fps;

        if( f->dictionary )
	{
		char **items = vevo_list_properties(f->dictionary );
		if(items)
		{
       			int i;
       		 	for( i = 0; items[i] != NULL ; i ++ )
       			{
       	        		 srt_seq_t *s = NULL;
                	 	 if( vevo_property_get( f->dictionary, items[i], 0,&s ) == VEVO_NO_ERROR )
                		        index_node_remove( f, s );
                		free(items[i]);
			}
       			free(items);
		}
	}
	
	if(f->index)
	{
		free(f->index);
		f->index = NULL;
	}

	f->index = (srt_cycle_t**) vj_calloc(sizeof(srt_cycle_t*)*(len+1));
	f->index_len = len;

	long k;
	for( k = 0; k <= f->index_len; k ++ )
		f->index[k] = (srt_cycle_t*) vj_calloc(sizeof(srt_cycle_t));

	if(dict)
	{
		f->dictionary = dict;
		char **items = vevo_list_properties( f->dictionary );
		if( items )
		{
			int i;
			for( i = 0; items[i] != NULL  ; i ++ )
			{
				srt_seq_t *s = NULL;
				if( vevo_property_get(dict, items[i],0, &s ) == VEVO_NO_ERROR )
					index_node_append(f,s );
				free(items[i]);
			}
			free(items);
		}
	}
}

void	vj_font_set_dict( void *font, void *dict )
{
	vj_font_t *f = (vj_font_t*) font;
	f->dictionary = dict;
}

void	*vj_font_get_dict(void *font)
{
	vj_font_t *f = (vj_font_t*) font;
	return f->dictionary;
}

static	int	compare_strings( char **p1, char **p2 )
{
	return strcoll( *p1, *p2 );
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
				if( strcasecmp( default_fonts[j].name, f->font_list[i] ) == 0 )
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
	
	int tmp = (w / 100) * 4;
	if(tmp>18) tmp = 18;
	f->current_size = (is_osd ? (tmp): 40);

	f->fps  = fps;
	f->index = NULL;
	f->font_table = (char**) vj_calloc(sizeof(char*) * MAX_FONTS );
	f->font_list = (char**) vj_calloc(sizeof(char*) * MAX_FONTS);
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
	find_fonts(f,"~/veejay-fonts");
	
	if( f->font_index <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No TrueType fonts found");
		vj_font_destroy( f );
		return NULL;
	}
	qsort( f->font_table, f->font_index, sizeof(char*), compare_strings );
	qsort( f->font_list,  f->font_index, sizeof(char*), compare_strings );

	int df = get_default_font( f );
	
	while(!configure( f, f->current_size, df ))
	{
		f->current_font ++;
		if( f->current_font >= f->font_index )
		{
			veejay_msg(0, "No more fonts to try");
			vj_font_destroy( f );
		}		
		return NULL;
	}

	f->time = is_osd;
	
	//print_fonts(f);

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


	uint8_t *Y = picture->data[0];
	uint8_t *U = picture->data[1];
	uint8_t *V = picture->data[2];

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
		      			if ( (r-1 >= 0) && (! bitmap->buffer[(r-1)*bitmap->pitch +c/8] & (0x80>>(c%8))) )
					{
						Y[ top ] = yuv_lncolor[0];	
						U[ top ] = yuv_lncolor[1];
						V[ top ] = yuv_lncolor[2]; 
					}
			
					if ( (r+1 < height) && (! bitmap->buffer[(r+1)*bitmap->pitch +c/8] & (0x80>>(c%8))) )
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
		if (  ((c == '_') && (text == f->text) ) || /* skip '_' (consider as space) 
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

static void vj_font_text_osd_render(vj_font_t *f, long posi, void *_picture, char *in_string )
{
	FT_Face face = f->face;
	FT_GlyphSlot  slot = face->glyph;
	FT_Vector pos[MAXSIZE_TEXT];  
	FT_Vector delta;
	char osd_text[1024];
	unsigned char c;
	int x = 0, y = 0, i=0;
	int str_w, str_w_max;

	VJFrame *picture = (VJFrame*) _picture;
	int width = picture->width;
	int height = picture->height;
	int x1,y1;
	str_w = str_w_max = 0;

	int size = 0;
	if(f->time > 2 )
	{
		if(in_string)
		{
			size = strlen( in_string );
			strncpy(osd_text, in_string, size );
			osd_text[size] = '\0';
		}
	}
	else
	if(f->time == 2 )
	{
		vj_font_print_credits(f,osd_text);
		size = strlen(osd_text);

	}
	else
	{
		unsigned char *tmp_text = vj_font_pos_to_timecode( f, posi );

		if( f->add )
			sprintf(osd_text, "%s %s", tmp_text, f->add );
		else
			sprintf(osd_text, "%s", tmp_text );
		size = strlen( osd_text );
		free(tmp_text);

		y = picture->height - f->current_size - 4;

	}

	if( size <= 0 )
		return;

	x1 = x;
	y1 = y;	
	unsigned int str_wi = 0;
	char *text = osd_text;

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
	
	int bw = str_w_max;
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
		if (  ((c == '_') && (text == f->text) ) || /* skip '_' (consider as space) 
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


int	vj_font_norender(void *ctx, long position)
{
	if(!ctx)
		return 0;
	vj_font_t *f = (vj_font_t *) ctx;

	if(f->time)
		return 1;
	
	if( position < 0 || position > f->index_len )
		return 0;

	if(!f->dictionary || !f->index )
		return 0;

	if(!f->index[position])
		return 0;

	int work = 0;
	int k = 0;
	for( k = 0; k <16; k ++ )
		if( f->index[position]->id[k] )
			work ++;
	return work;
}

void vj_font_render(void *ctx, void *_picture, long position, char *in_string)
{
	vj_font_t *f = (vj_font_t *) ctx;

	if(f->time)
	{
		vj_font_text_osd_render( f, position, _picture, in_string );
		return;
	}

	if( position < 0 || position > f->index_len )
		return;

	srt_cycle_t *list = f->index[ position ];

#ifdef STRICT_CHECKING
	assert( f->dictionary != NULL );
#endif

	font_lock( f );
	int k;
	for( k = 0; k < 16 ; k ++ )
	{
		if( list->id[k] == 0 )
			continue;

		srt_seq_t *s = NULL;
		char *key = make_key( list->id[k] );

		if( vevo_property_get( f->dictionary, key, 0, &s ) == VEVO_NO_ERROR )
		{
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

			memcpy( f->alpha, s->alpha, sizeof(s->alpha) );
			
			vj_font_text_render( f,s, _picture );
		}
		free(key);
	}
	font_unlock(f);
}



#endif

