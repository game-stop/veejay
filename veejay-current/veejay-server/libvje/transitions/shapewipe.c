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
#include "../libel/avcommon.h"
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


static char **shapelist = NULL;
static int shapeidx = 0;
static int currentshape = -1;
static void *selected_shape = NULL;
static int shape_min = 0;
static int shape_max = 0;
static int shape_completed = 0;

static void init_shape_loader()
{
    int maxshapes = MAX_NUMBER_OF_SHAPES;
    shapelist = (char**) vj_calloc(sizeof(char*) * maxshapes );
    load_shapes( shapelist, &shapeidx, maxshapes);
}

static void *change_shape(void *oldshape, int shape, int w, int h)
{
    if(oldshape)
        vj_picture_cleanup( oldshape );

    return vj_picture_open( shapelist[shape], w,h, PIX_FMT_GRAY8 );
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

static void shape_wipe_1( uint8_t *dst[3], uint8_t *src[3], uint8_t *pattern, const int len, const int threshold)
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

static void shape_wipe_2( uint8_t *dst[3], uint8_t *src[3], uint8_t *pattern, const int len, const int threshold)
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


int shapewipe_ready(int w, int h)
{
    return shape_completed;
}

vj_effect *shapewipe_init(int w, int h)
{
    init_shape_loader();

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = shapeidx - 1;
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
        char **hints = get_shapelist_hints( shapelist, ve->limits[1][0] );
        vje_build_value_hint_list_array( ve->hints, ve->limits[1][0], 0, hints );
        free_shapelist_hints(hints, ve->limits[1][0]);
    }
    else {
        veejay_msg(0, "You didn't put any shape transitions in $HOME/.veejay/shapes, I have nothing to do!");
    }

    vje_build_value_hint_list( ve->hints, ve->limits[1][2], 2, "White to Black", "Black to White" );

    char *home = getenv("HOME");
    veejay_msg(VEEJAY_MSG_INFO, "Put your shape transition files (png, pgm, tiff) in %s/.veejay/shapes", home ); 
    veejay_msg(VEEJAY_MSG_INFO, "Loaded %d shape transitions from storage", shapeidx - 1);

    ve->is_transition_ready_func = shapewipe_ready;

    return ve;
}

int shapewipe_malloc(int w, int h) {
    return 1;
}

void shapewipe_free() {
    if(selected_shape) {
        vj_picture_cleanup( selected_shape );
        selected_shape = NULL;
        currentshape = -1;
    }
}

int shapewipe_apply1( VJFrame *frame, VJFrame *frame2, double timecode, int shape, int threshold, int direction, int automatic)
{
    if( shape != currentshape) {
        selected_shape = change_shape( selected_shape, shape, frame->width, frame->height );
        if(selected_shape == NULL) {
            veejay_msg(0, "Unable to read %s", shapelist[ shape ] );
            return 0;
        }
        currentshape = shape;
        VJFrame *tmp = vj_picture_get( selected_shape );
        shape_find_min_max( tmp->data[0], tmp->len, &shape_min, &shape_max );
    }

    VJFrame *s = vj_picture_get( selected_shape );

    int auto_threshold = threshold;
    int range = (shape_max - shape_min);

    if(direction) {

        if(automatic)
            auto_threshold = (int) ( timecode * range ) + shape_min;

        shape_wipe_1( frame->data, frame2->data, s->data[0], frame->len, auto_threshold );
        
        if( auto_threshold >= shape_max )
            return 1;
    }
    else {

        if(automatic)
            auto_threshold = (int) range - (timecode * range ) + shape_min;

        shape_wipe_2( frame->data, frame2->data, s->data[0], frame->len, auto_threshold );

        if( auto_threshold <= shape_min )
            return 1;
    }

    return 0;
}

void shapewipe_apply( VJFrame *frame, VJFrame *frame2, double timecode, int shape, int threshold, int direction, int automatic)
{
    shape_completed = shapewipe_apply1(frame,frame2,timecode,shape,threshold,direction,automatic);
}
