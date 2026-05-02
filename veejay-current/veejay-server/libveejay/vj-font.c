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
#include <ctype.h>
#include <veejaycore/defs.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <libvje/vje.h>
#include <libvje/effects/common.h>
#include <libsubsample/subsample.h>
#include <veejaycore/libvevo.h>
#include <veejaycore/mjpeg_logging.h>
#include <veejaycore/yuv4mpeg.h>
#include <veejaycore/mpegconsts.h>
#include <veejaycore/mpegtimecode.h>
#include <veejaycore/vjmem.h>
#include <pthread.h>
#include <libveejay/vj-lib.h>
#ifdef HAVE_XML2
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#endif
#include <libvjxml/vj-xml.h>
extern  int vj_tag_size();

#ifdef HAVE_FREETYPE
#include <fcntl.h>
#include <ft2build.h>
#include <fontconfig/fontconfig.h>
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_FREETYPE_H
#include FT_GLYPH_H

#define BSIZE 256

typedef struct
{   
    int id;
    unsigned char *text;
    long  start;
    long  end;
    unsigned int x;
    unsigned int y;
    int size;
    int font;
    unsigned char *key;
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
    int id[16];
} srt_cycle_t;

typedef struct {
    unsigned char *text;
    unsigned char *font;
    uint8_t *fd_buf;
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
    int    text_width;
    int    current_font;
    int        baseline;
    int use_kerning;
    int current_size;
    unsigned char   **font_table;
    unsigned char    **font_list;
    int auto_number;
    int font_index;
    long        index_len;
    srt_seq_t   **index;
    float       fps;
    void    *dictionary;
    void    *plain;
    char    *prev;
    pthread_mutex_t mutex;

    srt_seq_t  *osd_sub;
    int         osd_prepared;

    int  text_buflen;
    unsigned char text_buffer[2048];

} vj_font_t;

static int  configure(vj_font_t *f, int size, int font);
static char *make_key(int id);
static char *vj_font_pos_to_timecode( vj_font_t *font, long pos );
static long vj_font_timecode_to_pos( vj_font_t *font, const char *tc );
static srt_seq_t    *vj_font_new_srt_sequence(vj_font_t *font, int id,unsigned char *text, long lo, long hi );
static void vj_font_del_srt_sequence( vj_font_t *f, int seq_id );
static void vj_font_store_srt_sequence( vj_font_t *f, srt_seq_t *s );
static unsigned char *select_font( vj_font_t *ec, int id );
static void vj_font_substract_timecodes( vj_font_t *font, unsigned char *tc_srt, long *lo, long *hi );
static unsigned char *vj_font_split_strd( unsigned char *str );
static unsigned char *vj_font_split_str( unsigned char *str );
static  int get_default_font( vj_font_t *f );

static  char    selected_default_font[1024];

static  void    font_lock(vj_font_t *f)
{
    pthread_mutex_lock( &(f->mutex) );
}
static  void    font_unlock( vj_font_t *f )
{
    pthread_mutex_unlock( &(f->mutex) );
}

static char *make_key(int id)
{
    char key[32];
    snprintf(key,sizeof(key),"s%d",id);
    return vj_strdup(key);
}



int vj_font_prepare( void *font, long s1, long s2 )
{
    vj_font_t *f = (vj_font_t*) font;
    if( f == NULL ) {
        return 0;
    }

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

int vj_font_srt_sequence_exists( void *font, int id )
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

static  char    *vj_font_pos_to_timecode( vj_font_t *font, long pos )
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

static  long    vj_font_timecode_to_pos( vj_font_t *font, const char *tc )
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

static  void    vj_font_substract_timecodes( vj_font_t *font, unsigned char *tc_srt, long *lo, long *hi )
{
    char tc1[20] = { 0 };
    char tc2[20] = { 0 };

    sscanf( (char*) tc_srt, "%s %*s %s", tc1,tc2 );

    *lo = vj_font_timecode_to_pos( font, tc1 );
    *hi = vj_font_timecode_to_pos( font, tc2 );
}

static unsigned char *vj_font_split_strd(unsigned char *str)
{
    if (!str || *str == '\0')
        return NULL;

    unsigned char *p = str;
    int i = 0;

    while (*p != '\0' && *p != '\n') {
        p++;
        i++;
    }

    if (*p == '\n') { p++; i++; }
    if (*p == '\n') { p++; i++; }

    if (i <= 0)
        return NULL;

    unsigned char *res = (unsigned char *)vj_calloc(i + 1);
    if (!res)
        return NULL;

    memcpy(res, str, i);
    res[i] = '\0';
    
    return res;
}

static unsigned char *vj_font_split_str(unsigned char *str)
{
    if (!str || *str == '\0')
        return NULL;

    unsigned char *p = str;
    int i = 0;

    while (*p != '\0' && *p != '\n') {
        p++;
        i++;
    }

    if (*p == '\n') {
        p++;
        i++;
    }

    if (i <= 0)
        return NULL;

    unsigned char *res = (unsigned char *)vj_calloc(i + 1);
    if (!res)
        return NULL;

    memcpy(res, str, i);
    res[i] = '\0'; 
    
    return res;
}

static srt_seq_t    *vj_font_new_srt_sequence( vj_font_t *f,int id,unsigned char *text, long lo, long hi )
{
    char tmp_key[16];
    srt_seq_t *s = (srt_seq_t*) vj_calloc(sizeof( srt_seq_t ));
    s->id   = id;
    s->text = (unsigned char*) vj_strdup( (const char*)text );
    s->start   = lo;
    s->end   = hi;
    snprintf(tmp_key,sizeof(tmp_key)-1, "s%d", id );
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
    s->key  = (unsigned char*) vj_strdup(tmp_key);

    veejay_msg(VEEJAY_MSG_DEBUG, "New SRT sequence: '%s' starts at position %ld , ends at position %ld", text,lo,hi );

    return s;
}

void            vj_font_set_current( void *font , int cur )
{
    vj_font_t *f = (vj_font_t*) font;
    if(vj_font_srt_sequence_exists( font, cur ))
    {
        f->auto_number = cur;
    }

}

static  void        vj_font_del_srt_sequence( vj_font_t *f, int seq_id )
{
    if(seq_id == 0 )
        seq_id = f->auto_number;
    
    char *key = make_key(seq_id );
    void *srt = NULL;

    
    int error = vevo_property_get( f->dictionary, (const char*) key, 0, &srt ) ;
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

static void     vj_font_store_srt_sequence( vj_font_t *f, srt_seq_t *s )
{
    void *srt = NULL;
    int error = vevo_property_get( f->dictionary, (const char*) s->key, 0, &srt );
    if( error == VEVO_NO_ERROR )
    {
        srt_seq_t *old = (srt_seq_t*) srt;

        veejay_msg(VEEJAY_MSG_DEBUG, "replacing subtitle %d, '%s', %ld -> %ld",
                old->id, old->text,old->start,old->end );
        free(old->text);
        free(old->key);
        free(old);
    }

    error = vevo_property_set( f->dictionary, (const char*) s->key, VEVO_ATOM_TYPE_VOIDPTR, 1,&s );
    if( error != VEVO_NO_ERROR )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to store SRT sequence '%s'", s->key );
    }
}

int vj_font_load_srt( void *font, const char *filename )
{
    vj_font_t *ff = (vj_font_t*) font;
    if( ff == NULL )
        return 0;

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
        fclose(f);
        return 0;
    }
    rewind( f );

    ff->fd_buf = (uint8_t*) vj_calloc( len );
    if(!ff->fd_buf) {
        fclose(f);
        return 0;
    }
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
            free(line);
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
            free(line);
            free(timecode);
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
            free(line);
            free(timecode);
            free(text);
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

void    vj_font_xml_unpack( xmlDocPtr doc, xmlNodePtr node, void *font )
{
    if(!node)
        return;
    vj_font_t *f = (vj_font_t*) font;
    if( f == NULL )
        return;

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
    if( vevo_property_get( f->dictionary,(const char*) key, 0, &s ) == VEVO_NO_ERROR )
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

void    vj_font_xml_pack( xmlNodePtr node, void *font )
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
                if ( vevo_property_get( ff->dictionary, (const char*) items[i], 0, &srt ) == VEVO_NO_ERROR )
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
        if ( vevo_property_get( ff->dictionary, (const char*) items[i], 0, &srt ) == VEVO_NO_ERROR )
        {
            srt_seq_t *s = (srt_seq_t*) srt;
            int n = strlen((const char*) s->text);
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
    if(ff == NULL)
        return NULL;

    char tmp[1024];
    srt_seq_t *s = NULL;
    void *srt = NULL;

    if( seq == 0 )
        seq = ff->auto_number;
    char *key = make_key( seq );    

    if( vevo_property_get( ff->dictionary, (const char*) key, 0, &srt ) != VEVO_NO_ERROR )
        return NULL;
    
    s = (srt_seq_t*) srt;
    
    int tcl1, tcl2;
    char *tc1 = vj_font_pos_to_timecode( ff, s->start );
    char *tc2 = vj_font_pos_to_timecode( ff, s->end );
    int len = strlen((const char*) s->text);
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

int vj_font_new_text( void *font, unsigned char *text, long lo,long hi, int seq)
{
    vj_font_t *ff = (vj_font_t*) font;
    if( ff == NULL )
        return 0;

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
    if( ff == NULL )
        return;
    
    ff->auto_number = seq;
    font_lock( ff );
    vj_font_del_srt_sequence( ff, seq);
    font_unlock( ff );
}

void    vj_font_set_position( void *font, int x, int y )
{
    vj_font_t *ff = (vj_font_t*) font;
    if( ff == NULL )
        return;

    int seq = ff->auto_number;

    char *key = make_key( seq );    

    srt_seq_t *s = NULL;
    if( vevo_property_get( ff->dictionary, (const char*) key, 0, &s ) != VEVO_NO_ERROR )
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
    if( ff == NULL || f_id < 0 || f_id >= ff->font_index  || size < 6 || size > 400 )
        return ;
    int seq = ff->auto_number;

    const char *key = make_key( seq );    

    srt_seq_t *s = NULL;
    if( vevo_property_get( ff->dictionary, key, 0, &s ) != VEVO_NO_ERROR )
    {
        free( (char*) key);
        return;
    }

    font_lock( ff );
    
    s->font = f_id;
    s->size  = size;

    veejay_msg(VEEJAY_MSG_DEBUG, "Selected font '%s', size %d",
        ff->font_list[s->font], s->size );
    
    font_unlock( ff );

    free((char*) key);  
}

void    vj_font_update_text( void *font, long s1, long s2, int seq, char *text)
{
    vj_font_t *ff = (vj_font_t*) font;
    if( ff == NULL )
        return;

    srt_seq_t *s = NULL;
    void *srt = NULL;
    if(seq == 0 )
        seq = ff->auto_number;
    const char *key = make_key( seq );
    int error = vevo_property_get( ff->dictionary, key, 0, &srt );
    if( error != VEVO_NO_ERROR )
    {
        veejay_msg(0, "No such subtitle '%s'",key);
        free((char*) key);
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
                free((char*)key);
                return;
            }
        }
    }

    s = (srt_seq_t*) srt;
    font_lock( ff );
    if( s->text )
        free(s->text);
    s->text = (unsigned char*) vj_strdup( (const char*) text );
    s->start = s1;
    s->end = s2;

    ff->auto_number = seq;

    font_unlock( ff );
    free( (void*) key);  
}

char **vj_font_get_sequences( void *font )
{
    vj_font_t *f = (vj_font_t*) font;
    if( f == NULL )
        return NULL;

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
        if( vevo_property_get( f->dictionary, (const char*) items[i], 0,&s ) == VEVO_NO_ERROR )
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

#define MAX_FONTS 512

char    **vj_font_get_all_fonts( void *font )
{
    vj_font_t *f = (vj_font_t*) font;
    if( f == NULL )
        return NULL;
    
    int i;
    if( f->font_index <= 0 )
        return NULL;
    
    char **res = (char**) vj_calloc(sizeof(char*) * (f->font_index +1) );
    for( i =0; i < f->font_index ;i ++ )
        res[i] = vj_strdup( (char*) f->font_list[i] );
    return res;
}

static unsigned char    *select_font( vj_font_t *ec, int id )
{
    if( id < 0 || id >= ec->font_index )
        return NULL;

    return ec->font_table[id];   
}

void vj_font_set_outline_and_border( void *font, int outline, int border)
{
    vj_font_t *ff = (vj_font_t*) font;
    if( ff == NULL )
        return;

    int seq = ff->auto_number;
    const char *key = make_key( seq );    

    srt_seq_t *s = NULL;
    if( vevo_property_get( ff->dictionary, key, 0, &s ) != VEVO_NO_ERROR )
    {
        free((void*)key);
        return;
    }
    font_lock( ff );

    s->use_bg  = border;
    s->outline = outline;
    
    font_unlock( ff );

    free((void*)key);
}

void vj_font_set_lncolor( void *font, int r, int g, int b, int a)
{
    vj_font_t *f = (vj_font_t*) font;
    
    int seq = f->auto_number;

    const char *key = make_key( seq );    

    srt_seq_t *s = NULL;
    if( vevo_property_get( f->dictionary, key, 0, &s ) != VEVO_NO_ERROR )
    {
        free((void*) key);
        return;
    }
    font_lock( f );

    s->ln[0] = r;
    s->ln[1] = g;
    s->ln[2] = b;
    s->alpha[2] = a;
    
    font_unlock( f );

    free((void*) key);
}

void vj_font_set_bgcolor( void *font, int r, int g, int b,int a)
{
    vj_font_t *f = (vj_font_t*) font;
    
    int seq = f->auto_number;
    const char *key = make_key( seq );    

    srt_seq_t *s = NULL;
    if( vevo_property_get( f->dictionary, key, 0, &s ) != VEVO_NO_ERROR )
    {
        free((void*)key);
        return;
    }
    font_lock( f );

    s->bg[0] = r;
    s->bg[1] = g;
    s->bg[2] = b;
    s->alpha[1] = a;
    
    font_unlock( f );

    free((void*) key);
}


void vj_font_set_fgcolor( void *font, int r, int g, int b, int a)
{
    vj_font_t *ff = (vj_font_t*) font;
    int seq = ff->auto_number;

    char *key = make_key( seq );    

    srt_seq_t *s = NULL;
    if( vevo_property_get( ff->dictionary, key, 0, &s ) != VEVO_NO_ERROR )
    {
        free((void*)key);
        return;
    }
    font_lock( ff );

    s->fg[0] = r;
    s->fg[1] = g;
    s->fg[2] = b;
    s->alpha[0] = a;
    
    font_unlock( ff );

    free((void*)key);
}

void    vj_font_dictionary_destroy( void *font, void *dict )
{
    if(!dict)
        return;
        
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


void    vj_font_destroy(void *ctx)
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
  
    free( f->font_table );
    free( f->font_list );
    free( f );
}

static  void    fallback_font(vj_font_t *f)
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

static int  configure(vj_font_t *f, int size, int font)
{
    int c,error;
    FT_BBox bbox;
    int yMax,yMin,xMax,xMin;

    f->font = select_font( f , font );

    if(f->font == NULL )
    {
        fallback_font( f );
    }

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
    int face_index = 0;

    if ( (error = FT_New_Face( f->library, (char*)f->font, face_index, &(f->face) )) != 0)
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

	snprintf(selected_default_font, sizeof(selected_default_font), "%s", f->font);
    
    f->use_kerning  =   FT_HAS_KERNING(f->face);
    
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
            f->bitmaps[c]       = f->face->glyph->bitmap;
            f->bitmap_left[c]   = f->face->glyph->bitmap_left;
            f->bitmap_top[c]    = f->face->glyph->bitmap_top;
            f->advance[c]       = f->face->glyph->advance.x >> 6;

            
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

void    *vj_font_get_plain_dict( void *font )
{
    vj_font_t *f = (vj_font_t*) font;
    return f->plain;
}

void vj_font_set_osd_text(void *font, char *text)
{
    if (!font || !text)
        return;

    vj_font_t *f = (vj_font_t*) font;

    size_t len = strnlen(text, sizeof(f->text_buffer) - 1);
    memcpy(f->text_buffer, text, len);
    f->text_buffer[len] = '\0';

    f->text_buflen = len;
}

void    vj_font_set_dict( void *font, void *dict )
{
    vj_font_t *f = (vj_font_t*) font;
    if( f == NULL ) 
        return;
    f->dictionary = dict;
}

void    *vj_font_get_dict(void *font)
{
    vj_font_t *f = (vj_font_t*) font;
    if(f == NULL )
        return NULL;
    return f->dictionary;
}

static int get_default_font(vj_font_t *f)
{
    if (!f || f->font_index == 0)
        return 0;

    static const char *prefer[] = {
        "Mono",
        "Code",
        "Courier",
        "Console",
        NULL
    };

    for (int p = 0; prefer[p] != NULL; ++p) {
        for (int i = 0; i < f->font_index; ++i) {
            if (!f->font_list[i])
                continue;

            if (strcasestr((char*)f->font_list[i], prefer[p])) {
                veejay_msg(VEEJAY_MSG_DEBUG,
                           "Using monospaced default font '%s'",
                           f->font_list[i]);
                return i;
            }
        }
    }

    for (int i = 0; i < f->font_index; ++i) {
        if (!f->font_list[i])
            continue;

        if (strcasestr((char*)f->font_list[i], "Sans") &&
            strcasestr((char*)f->font_list[i], "Mono")) {
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "Using fallback mono font '%s'",
                       f->font_list[i]);
            return i;
        }
    }

    veejay_msg(VEEJAY_MSG_DEBUG,
               "No monospaced font found, using first available '%s'",
               f->font_list[0] ? (char*)f->font_list[0] : "unknown");

    return 0;
}

int calc_osd_font_size(int screen_width, int is_osd)
{
    if (!is_osd) return 40;
    int tmp = (int)(screen_width * 0.08);
    if (tmp > 48) tmp = 48;
    if (tmp < 12) tmp = 12;
    return tmp;
}

static int vj_font_list_truetype(vj_font_t *f)
{
    if (!FcInit()) {
        veejay_msg(VEEJAY_MSG_ERROR, "Fontconfig initialization failed");
        return 0;
    }

    FcPattern *pat = FcPatternCreate();
    if (!pat)
        return 0;

    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);

    FcObjectSet *os = FcObjectSetBuild(FC_FILE, FC_FAMILY, FC_FONTFORMAT, FC_INDEX, NULL);
    if (!os) {
        FcPatternDestroy(pat);
        return 0;
    }

    FcFontSet *fs = FcFontList(NULL, pat, os);
    if (!fs) {
        FcObjectSetDestroy(os);
        FcPatternDestroy(pat);
        return 0;
    }

    int count = 0;

    for (int i = 0; i < fs->nfont && count < MAX_FONTS; i++) {
        FcPattern *font = fs->fonts[i];

        FcChar8 *file = NULL;
        FcChar8 *family = NULL;
        FcChar8 *format = NULL;

        if (FcPatternGetString(font, FC_FILE, 0, &file) != FcResultMatch)
            continue;

        if (FcPatternGetString(font, FC_FONTFORMAT, 0, &format) != FcResultMatch)
            continue;

        if (strcmp((char*)format, "TrueType") != 0 &&
            strcmp((char*)format, "CFF") != 0)
            continue;

        FcPatternGetString(font, FC_FAMILY, 0, &family);

        f->font_table[count] = (unsigned char*) strdup((char*)file);
        f->font_list[count]  = family
                                ? (unsigned char*) strdup((char*)family)
                                : (unsigned char*) strdup((char*)file);

        count++;
    }

    f->font_index = count;

    FcFontSetDestroy(fs);
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);

    return count;
}

int      vj_font_init_once() {
    if(!FcInit()) {
        veejay_msg(0, "FontConfig intitialization failed");
        return -1;
    }

    return 0;
}

void    *vj_font_init( int w, int h, float fps, int is_osd )
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
    f->current_size = calc_osd_font_size(w, is_osd);

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

    if (!vj_font_list_truetype(f) || f->font_index == 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "No usable TrueType fonts found via Fontconfig");
        vj_font_destroy(f);
        return NULL;
    }

    veejay_msg(VEEJAY_MSG_DEBUG,
               "Loaded %d TrueType fonts (Fontconfig)",
               f->font_index);

    f->current_font = get_default_font( f );
    
    while(!configure( f, f->current_size, f->current_font ))
    {
        f->current_font ++;
        if( f->current_font >= f->font_index )
        {
            veejay_msg(0, "It seems that all loaded fonts are not working. Bye");
            vj_font_destroy( f );
            return NULL;
        }       
    }

    pthread_mutex_init( &(f->mutex), NULL );
    
    return f;
}

void    *vj_font_single_init( int w, int h, float fps,char *path )
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

    if (!vj_font_list_truetype(f) || f->font_index == 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "No usable TrueType fonts found via Fontconfig");
        vj_font_destroy(f);
        return NULL;
    }

    f->current_font = get_default_font( f );
    
    while(!configure( f, f->current_size, f->current_font ))
    {
        f->current_font ++;
        if( f->current_font >= f->font_index )
        {
            veejay_msg(0, "It seems that all loaded fonts are not working. Bye");
            vj_font_destroy( f );
            return NULL;
        }       
    }

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
    uint8_t *Y = picture->data[0];
    uint8_t *U = picture->data[1];
    uint8_t *V = picture->data[2];

    const uint32_t bitmap_rows  = bitmap->rows;
    const uint32_t bitmap_wid   = bitmap->width;
    const int      bitmap_pitch = bitmap->pitch;
    uint8_t *bitbuffer          = bitmap->buffer;

    const uint32_t max_r = (y + bitmap_rows > height)
                         ? (height - y)
                         : bitmap_rows;

    const uint32_t max_c = (x + bitmap_wid > width)
                         ? (width - x)
                         : bitmap_wid;

    for (uint32_t r = 0; r < max_r; r++)
    {
        const uint32_t dst_row_offset = (y + r) * width;
        uint8_t *src_row = bitbuffer + r * bitmap_pitch;

        for (uint32_t c = 0; c < max_c; c++)
        {
            if (!(src_row[c >> 3] & (0x80 >> (c & 7))))
                continue;

            const uint32_t dst_index = dst_row_offset + x + c;

            Y[dst_index] = yuv_fgcolor[0];
            U[dst_index] = yuv_fgcolor[1];
            V[dst_index] = yuv_fgcolor[2];

            if (!outline)
                continue;

            if (c > 0)
            {
                if (!(src_row[(c - 1) >> 3] & (0x80 >> ((c - 1) & 7))))
                {
                    uint32_t idx = dst_index - 1;
                    Y[idx] = yuv_lncolor[0];
                    U[idx] = yuv_lncolor[1];
                    V[idx] = yuv_lncolor[2];
                }
            }

            if (c < max_c - 1)
            {
                if (!(src_row[(c + 1) >> 3] & (0x80 >> ((c + 1) & 7))))
                {
                    uint32_t idx = dst_index + 1;
                    Y[idx] = yuv_lncolor[0];
                    U[idx] = yuv_lncolor[1];
                    V[idx] = yuv_lncolor[2];
                }
            }

            if (r > 0)
            {
                uint8_t *prev_row = bitbuffer + (r - 1) * bitmap_pitch;

                if (!(prev_row[c >> 3] & (0x80 >> (c & 7))))
                {
                    uint32_t idx = dst_index - width;
                    Y[idx] = yuv_lncolor[0];
                    U[idx] = yuv_lncolor[1];
                    V[idx] = yuv_lncolor[2];
                }
            }

            if (r < max_r - 1)
            {
                uint8_t *next_row = bitbuffer + (r + 1) * bitmap_pitch;

                if (!(next_row[c >> 3] & (0x80 >> (c & 7))))
                {
                    uint32_t idx = dst_index + width;
                    Y[idx] = yuv_lncolor[0];
                    U[idx] = yuv_lncolor[1];
                    V[idx] = yuv_lncolor[2];
                }
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
    uint8_t *A[3] = { picture->data[0], picture->data[1], picture->data[2] };

    unsigned int x_end = x + width;
    unsigned int y_end = y + height;

    if (x_end > picture->width) x_end = picture->width;
    if (y_end > picture->height) y_end = picture->height;

    for (j = y; j < y_end; j++)
#pragma omp simd
        for (i = x; i < x_end; i++) {
            int p = i + (j * w);
            A[0][p] = (op0 * A[0][p] + op1 * yuv_color[0]) >> 8;
            A[1][p] = (op0 * A[1][p] + op1 * yuv_color[1]) >> 8;
            A[2][p] = (op0 * A[2][p] + op1 * yuv_color[2]) >> 8;
        }
}


static inline void draw_box(VJFrame *picture,
                            unsigned int x,
                            unsigned int y,
                            unsigned int width,
                            unsigned int height,
                            uint8_t *yuv_color)
{
    int i, j;
    unsigned int x_end = x + width;
    unsigned int y_end = y + height;

    if (x_end > picture->width) x_end = picture->width;
    if (y_end > picture->height) y_end = picture->height;

    for (j = y; j < y_end; j++)
        for (i = x; i < x_end; i++) {
            int p = i + (j * picture->width);
            picture->data[0][p] = yuv_color[0];
            picture->data[1][p] = yuv_color[1];
            picture->data[2][p] = yuv_color[2];
        }
}

#define MAXSIZE_TEXT 1024
static void vj_font_text_render(vj_font_t *f, srt_seq_t *seq, void *_picture)
{
    if (!f || !seq || !_picture || !seq->text || strlen( (const char*) seq->text) == 0)
        return;

    int size = strlen((const char*) seq->text);
    FT_Face face = f->face;
    FT_Vector pos[MAXSIZE_TEXT];
    FT_Vector delta;
    unsigned char c;
    int i;

    VJFrame *picture = (VJFrame*) _picture;
    int pic_width = picture->width;
    int pic_height = picture->height;

    int x1 = f->x;
    int y1 = f->y;
    int line_start_idx = 0;
    int last_space_idx = -1;
    int max_line_width = 0;
    int x_cursor = x1;
    int y_cursor = y1;

    unsigned char *text = seq->text;

    for (i = 0; i < size; i++) {
        c = text[i];

        if (f->use_kerning && i > 0 && f->glyphs_index[c]) {
            FT_Get_Kerning(face, f->glyphs_index[c], f->glyphs_index[c],
                           ft_kerning_default, &delta);
            x_cursor += delta.x >> 6;
        }
        if (c == ' ' || c == '\t')
            last_space_idx = i;

        int advance = (isblank(c) || c == 20) ? f->current_size : f->advance[c];

        if ((x_cursor + advance) >= pic_width || c == '\n') {
            int wrap_idx = i;
            if (x_cursor + advance >= pic_width && last_space_idx > line_start_idx) {
                wrap_idx = last_space_idx;
            }

            int j;
            int line_x = x1;
            for (j = line_start_idx; j <= wrap_idx; j++) {
                unsigned char cc = text[j];
                pos[j].x = line_x + f->bitmap_left[cc];
                pos[j].y = y_cursor - f->bitmap_top[cc] + f->baseline;
                line_x += (isblank(cc) || cc == 20) ? f->current_size : f->advance[cc];
            }

            if (line_x - x1 > max_line_width)
                max_line_width = line_x - x1;

            y_cursor += f->text_height;
            x_cursor = x1;
            line_start_idx = wrap_idx + 1;
            last_space_idx = -1;
        } else {
            x_cursor += advance;
        }
    }

    int line_x = x1;
    for (i = line_start_idx; i < size; i++) {
        unsigned char cc = text[i];
        pos[i].x = line_x + f->bitmap_left[cc];
        pos[i].y = y_cursor - f->bitmap_top[cc] + f->baseline;
        line_x += (isblank(cc) || cc == 20) ? f->current_size : f->advance[cc];
    }
    if (line_x - x1 > max_line_width)
        max_line_width = line_x - x1;

    int total_lines = (y_cursor - y1) / f->text_height + 1;
    int bh = total_lines * f->text_height + 4;

    if (f->bg) {
        if (f->alpha[1] == 0)
            draw_box(picture, x1, y1, max_line_width, bh, f->bgcolor);
        else
            draw_transparent_box(picture, x1, y1, max_line_width, bh, f->bgcolor, f->alpha[1]);
    }

    for (i = 0; i < size; i++) {
        c = text[i];

        if ((c == (unsigned char) '_') && (text == f->text))
            continue;
        if (c == '\n')
            continue;

        draw_glyph(f, picture, &(f->bitmaps[c]),
                   pos[i].x, pos[i].y,
                   pic_width, pic_height,
                   f->fgcolor, f->lncolor, f->outline);
    }
}

char    *vj_font_default(void)
{
    int n = strlen( selected_default_font );
    if( n <= 0 )
        return NULL;

    return selected_default_font;
}


int vj_font_norender(void *ctx, long position)
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
 

void vj_font_render(void *ctx, void *_picture, long position)
{
    if (!ctx || !_picture)
        return;

    vj_font_t *f = (vj_font_t *) ctx;
    font_lock(f);

    for (int i = 0; i < f->index_len; i++) {
        srt_seq_t *s = f->index[i];
        if (!s)
            continue;

        if (position < s->start || position > s->end)
            continue;

        int old_font = f->current_font;
        int old_size = f->current_size;

        if (old_font != s->font || old_size != s->size) {
            if (!configure(f, s->size, s->font))
                if (!configure(f, old_size, old_font))
                    break;
        }

        f->x = s->x;
        f->y = s->y;
        f->bg = s->use_bg;
        f->outline = s->outline;

        if (f->outline)
            _rgb2yuv(s->ln[0], s->ln[1], s->ln[2], f->lncolor[0], f->lncolor[1], f->lncolor[2]);

        _rgb2yuv(s->fg[0], s->fg[1], s->fg[2], f->fgcolor[0], f->fgcolor[1], f->fgcolor[2]);
        if (f->bg)
            _rgb2yuv(s->bg[0], s->bg[1], s->bg[2], f->bgcolor[0], f->bgcolor[1], f->bgcolor[2]);

        f->alpha[0] = s->alpha[0];
        f->alpha[1] = s->alpha[1];
        f->alpha[2] = s->alpha[2];

        vj_font_text_render(f, s, _picture);
    }

    font_unlock(f);
}

static void vj_font_prepare_osd(vj_font_t *f, int w)
{
    if (!f || f->osd_prepared)
        return;

    f->osd_sub = (srt_seq_t*) calloc(1, sizeof(srt_seq_t));
    if (!f->osd_sub)
        return;

    const float BASE_AT_1920 = 24.0;
    int MIN_SIZE = 8;
    int MAX_SIZE = 48;

    if (w <= 480) MIN_SIZE = 12;

    float sizef = (float)w * (BASE_AT_1920 / 1920.0f);
    int size = floorf(sizef + 0.5f);

    if (size < MIN_SIZE) size = MIN_SIZE;
    if (size > MAX_SIZE) size = MAX_SIZE;

    f->current_size = size;
    f->osd_sub->x = 1;
    f->osd_sub->y = -1;
    f->osd_sub->size = f->current_size;
    f->osd_sub->font = f->current_font;
    f->osd_sub->use_bg = 1;
    f->osd_sub->outline = 0;

    _rgb2yuv(255, 255, 255, f->fgcolor[0], f->fgcolor[1], f->fgcolor[2]);
    _rgb2yuv(0, 0, 0, f->bgcolor[0], f->bgcolor[1], f->bgcolor[2]);

    f->osd_prepared = 1;
}

static void vj_font_text_osd_render(vj_font_t *f, void *_picture, int x, int y)
{
    if (!f || !_picture || f->text_buflen <= 0)
        return;

    VJFrame *picture = (VJFrame*) _picture;
    FT_Face face = f->face;
    unsigned char *text = f->text_buffer;
    int size = strlen((const char*) text);
    int width = picture->width;
    int height = picture->height;

    int line_count = 1;
    int cur_x = 0;
    int max_line_width = 0;
    unsigned char prev_c = 0;

    for (int i = 0; i < size; i++) {
        unsigned char c = text[i];
        if (c == '\n') {
            if (cur_x > max_line_width) max_line_width = cur_x;
            cur_x = 0;
            line_count++;
            prev_c = 0;
            continue;
        }
        if (f->use_kerning && prev_c > 0 && f->glyphs_index[c]) {
            FT_Vector delta;
            FT_Get_Kerning(face, f->glyphs_index[prev_c], f->glyphs_index[c], ft_kerning_default, &delta);
            cur_x += delta.x >> 6;
        }

        int adv = (isblank(c) || c == 20) ? f->current_size : f->advance[c];
        if ((cur_x + adv) > (width - x)) {
            if (cur_x > max_line_width) max_line_width = cur_x;
            cur_x = 0;
            line_count++;
            prev_c = 0;
        }

        cur_x += adv;
        prev_c = c;
    }
    if (cur_x > max_line_width) max_line_width = cur_x;

    int total_text_height = (line_count * f->text_height); 
    
    int bh = total_text_height + 12;
    int bw = max_line_width + 12;

    if (y == -1) {
        y = picture->height - bh - 12; 
        if (y < 0) y = 0;
    }

    draw_transparent_box(picture, x, y, bw, bh, f->bgcolor, 160);

    int draw_x = x + 2;
    int draw_y = y + 4;
    cur_x = 0;
    prev_c = 0;

    for (int i = 0; i < size; i++) {
        unsigned char c = text[i];
        int adv = (isblank(c) || c == 20) ? f->current_size : f->advance[c];

        if (c == '\n' || (cur_x + adv) > (width - x)) {
            draw_y += f->text_height;
            draw_x = x + 4;
            cur_x = 0;
            prev_c = 0;
            if (c == '\n') continue;
        }

        if (f->use_kerning && prev_c > 0 && f->glyphs_index[c]) {
            FT_Vector delta;
            FT_Get_Kerning(face, f->glyphs_index[prev_c], f->glyphs_index[c], ft_kerning_default, &delta);
            draw_x += delta.x >> 6;
        }

        int gx = draw_x + f->bitmap_left[c];
        int gy = draw_y + f->baseline - f->bitmap_top[c];

        if (c != '_' || text != f->text) {
             draw_glyph(f, picture, &(f->bitmaps[c]),
                       gx, gy,
                       width, height,
                       f->fgcolor, f->lncolor, f->outline);
        }

        draw_x += adv;
        cur_x += adv;
        prev_c = c;
    }
}

void vj_font_render_osd_status(void *ctx, void *_picture, char *status_str, int placement)
{
    if (!ctx || !_picture || !status_str)
        return;

    vj_font_t *f = (vj_font_t *) ctx;
    VJFrame *pic = (VJFrame*) _picture;

    if (!f->osd_prepared)
        vj_font_prepare_osd(f, pic->width);

    vj_font_set_osd_text(f, status_str);

    int x = 5;
    int y = (placement == 1) ? f->baseline : -1;

    font_lock(f);
    vj_font_text_osd_render(f, _picture, x, y);
    font_unlock(f);
}


#endif

