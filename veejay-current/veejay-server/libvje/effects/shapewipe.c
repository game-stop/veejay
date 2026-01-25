/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */
#include <config.h>    	
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

#include <libavutil/avutil.h>
#include "../effects/common.h"
#include "../libel/pixbuf.h"
#include <veejaycore/avcommon.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include "shapewipe.h"

#define MAX_NUMBER_OF_SHAPES 10000  // the amount of shapes this FX can hold

static int is_img( const char *file )
{
    if( strstr( file, ".png" ) || strstr( file, ".PNG" ) )
        return 1;

    if( strstr( file, ".pgm" ) || strstr( file, ".PGM" ) )
        return 1;

    if( strstr( file, ".tif" ) || strstr( file, ".TIF" ) || strstr( file, ".tiff" ) || strstr( file, ".TIFF" ) )
        return 1;

    return 0;
}

static int find_shape_file(char *path, char **shapelist, int *shapeidx, int maxshapes )
{
    if(!path)
        return 0;

    struct stat l;
    veejay_memset(&l, 0, sizeof(struct stat));
    if( lstat( path, &l ) < 0 )
        return 0;
    if( S_ISLNK(l.st_mode) ) {
        veejay_memset(&l,0,sizeof(struct stat));
        stat(path, &l );
    }
    if( S_ISDIR(l.st_mode)) {
        return 1;
    }
    if( S_ISREG(l.st_mode)) {
        if(is_img(path)) {
            if(*shapeidx < maxshapes) {
                shapelist[ *shapeidx ] = strdup(path);
                *shapeidx = *shapeidx + 1;
            }
        }
    }
    return 0;
}

static int find_shapes(char *path, char **shapelist, int *shapeidx, int maxshapes)
{
    struct dirent **files;
    int n = scandir(path, &files, NULL, alphasort);
    if( n < 0 )
        return 0;
    char tmp[2048];
    while( n -- ) {
        snprintf(tmp,sizeof(tmp), "%s/%s", path, files[n]->d_name );
        if(strcmp( files[n]->d_name, "." ) != 0 && strcmp( files[n]->d_name, ".." ) != 0 ) {
            if( find_shape_file(tmp, shapelist, shapeidx, maxshapes ) )
                find_shapes(tmp, shapelist, shapeidx, maxshapes );
        }
        free(files[n]);
    }
    free(files);
    return 1;
}

static void load_shapes(char **shapelist, int *shapeidx, int maxshapes)
{
    char *home = getenv("HOME");
    char path[2048];

    snprintf(path, sizeof(path), "%s/.veejay/shapes", home );
    find_shapes( path, shapelist, shapeidx, maxshapes );

    find_shapes( "/usr/local/share/veejay/shapes", shapelist, shapeidx, maxshapes );
    find_shapes( "/usr/share/veejay/shapes", shapelist, shapeidx, maxshapes );
}

typedef struct {
    char **shapelist;
    int shapeidx;
    int currentshape; // -1
    void *selected_shape;
    int shape_min;
    int shape_max;
    int shape_completed;
} shape_t;

static shape_t *init_shape_loader()
{
    shape_t *s = (shape_t*) vj_calloc(sizeof(shape_t));
    if(!s) {
        return NULL;
    }

    int maxshapes = MAX_NUMBER_OF_SHAPES;
    s->shapelist = (char**) vj_calloc(sizeof(char*) * maxshapes );
    if(!s->shapelist) {
        free(s);
        return NULL;
    }

    s->currentshape = -1;
    load_shapes( s->shapelist, &(s->shapeidx), maxshapes);

    return s;
}

static void free_shape_loader(shape_t *s)
{
    int i;
    for( i = 0; i < MAX_NUMBER_OF_SHAPES; i ++ ) {
        if(s->shapelist[i]) {
            free(s->shapelist[i]);
        }
    }
    free(s->shapelist);
    free(s);
}

static void *change_shape(shape_t *s, void *oldshape, int shape, int w, int h)
{
    if(oldshape)
        vj_picture_cleanup( oldshape );

    return vj_picture_open( s->shapelist[shape], w,h, PIX_FMT_GRAY8 );
}

static char **get_shapelist_hints(char **shapelist, int num )
{
    char **result = (char**) vj_calloc(sizeof(char*) * num );
    int i;
    for( i = 0; i <= num; i ++ ) {
        char *ptr = basename( shapelist[i] );
        result[i] = strdup(ptr);
    }
    return result;
}

static void free_shapelist_hints(char **hints, int num )
{
    int i;
    for( i = 0; i <= num; i ++ ) {
        free(hints[i]);
    }
    free(hints);
}

static void shape_find_min_max(uint8_t *data, const int len, int *min, int *max )
{
    int a=256,b=0;
    int i;
    for( i = 0; i < len; i ++ ) {
        if( data[i] > b )
            b = data[i];
        if( data[i] < a )
            a = data[i];
    }
    *min = a;
    *max = b;
}

static void shape_wipe_1( uint8_t *dst[4], uint8_t *src[4], uint8_t *pattern, const int len, const int threshold)
{
    int i;
    for( i = 0; i < len; i ++ ) 
    {
        if( pattern[i] < threshold)
        {
            dst[0][i] = src[0][i];
            dst[1][i] = src[1][i];
            dst[2][i] = src[2][i];
        }
    }
}

static void shape_wipe_2( uint8_t *dst[4], uint8_t *src[4], uint8_t *pattern, const int len, const int threshold)
{
    int i;
    for( i = 0; i < len; i ++ ) 
    {
        if( pattern[i] > threshold )
        {
            dst[0][i] = src[0][i];
            dst[1][i] = src[1][i];
            dst[2][i] = src[2][i];
        }
    }
}

int shapewipe_get_num_shapes(void *ptr)
{
    shape_t *s = (shape_t*) ptr;
    return s->shapeidx - 1;
}

int shapewipe_ready(void *ptr, int w, int h)
{
    shape_t *s = (shape_t*) ptr;
    return s->shape_completed;
}

vj_effect *shapewipe_init(int w, int h)
{
    shape_t *s = init_shape_loader();
    if(!s) {
        return NULL;
    }

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 256;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->defaults[0] = 0;
    ve->defaults[1] = 0;
    ve->defaults[2] = 0;
    ve->defaults[3] = 1;
    ve->description = "Shape Wipe";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->parallel = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Shape", "Threshold", "Direction", "Automatic"); 

    ve->hints = vje_init_value_hint_list( ve->num_params );

    if(ve->limits[1][0] > 0) {
        char **hints = get_shapelist_hints( s->shapelist, ve->limits[1][0] );
        vje_build_value_hint_list_array( ve->hints, ve->limits[1][0], 0, hints );
        free_shapelist_hints(hints, ve->limits[1][0]);
    }
    else {
        veejay_msg(0, "You didn't put any shape transitions in $HOME/.veejay/shapes, I have nothing to do!");
    }

    vje_build_value_hint_list( ve->hints, ve->limits[1][2], 2, "White to Black", "Black to White" );

    char *home = getenv("HOME");
    veejay_msg(VEEJAY_MSG_INFO, "Put your shape transition files (png, pgm, tiff) in %s/.veejay/shapes", home ); 

    ve->is_transition_ready_func = shapewipe_ready;
    ve->limits[1][0] = s->shapeidx - 1;
    
    veejay_msg(VEEJAY_MSG_INFO, "Loaded %d shape transitions from storage", s->shapeidx - 1);


    free_shape_loader(s);

    return ve;
}

void *shapewipe_malloc(int w, int h) {

    return init_shape_loader();
}

void shapewipe_free(void *ptr) {

    shape_t *s = (shape_t*) ptr;

    if(s->selected_shape) {
        vj_picture_cleanup( s->selected_shape );
    }

    free_shape_loader(s);
}

static int shapewipe_apply1(void *ptr, VJFrame *frame, VJFrame *frame2, double timecode, int shape, int threshold, int direction, int automatic)
{
    shape_t *s = (shape_t*) ptr;
    if( shape != s->currentshape) {
        s->selected_shape = change_shape( s,s->selected_shape, shape, frame->width, frame->height );
        if(s->selected_shape == NULL) {
            veejay_msg(0, "Unable to read %s", s->shapelist[ shape ] );
            return 0;
        }
        s->currentshape = shape;
        VJFrame *tmp = vj_picture_get( s->selected_shape );
        shape_find_min_max( tmp->data[0], tmp->len, &(s->shape_min), &(s->shape_max) );
    }

    VJFrame *src = vj_picture_get( s->selected_shape );

    int auto_threshold = threshold;
    int range = (s->shape_max - s->shape_min);

    if(direction) {

        if(automatic)
            auto_threshold = (int) ( timecode * range ) + s->shape_min;

        shape_wipe_1( frame->data, frame2->data, src->data[0], frame->len, auto_threshold );
        
        if( auto_threshold >= s->shape_max )
            return 1;
    }
    else {

        if(automatic)
            auto_threshold = (int) range - (timecode * range ) + s->shape_min;

        shape_wipe_2( frame->data, frame2->data, src->data[0], frame->len, auto_threshold );

        if( auto_threshold <= s->shape_min )
            return 1;
    }

    return 0;
}

void shapewipe_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    double timecode = frame->timecode;
    int shape = args[0];
    int threshold = args[1];
    int direction = args[2];
    int automatic = args[3];

    shape_t *s = (shape_t*) ptr;
    s->shape_completed = shapewipe_apply1(ptr, frame,frame2,timecode,shape,threshold,direction,automatic);
}

int shapewipe_process( void *ptr, VJFrame *frame, VJFrame *frame2,double timecode, int shape, int threshold, int direction, int automatic) {
    shape_t *s = (shape_t*) ptr;
    s->shape_completed = shapewipe_apply1(ptr, frame,frame2,timecode,shape,threshold,direction,automatic);
    return s->shape_completed;
}
